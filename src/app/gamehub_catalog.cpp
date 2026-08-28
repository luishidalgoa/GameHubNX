#include "gamehub_catalog.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// sha1.h is plain C and carries no extern "C" guard of its own, so the
// declarations must be wrapped here or C++ name mangling loses them at link.
extern "C" {
#include "../core/sha1.h"
}

namespace gamehubnx {
namespace gamehub {

const char* const kCatalogUrl  = "https://gamehub.hdglabs.com/api/shop";
const char* const kClientId    = "GameHubNX/1.0.0";

std::string baseTitleIdOf(const std::string& titleId) {
    if (titleId.size() != 16)
        return {};
    uint64_t value = 0;
    for (char c : titleId) {
        int digit;
        if (c >= '0' && c <= '9')      digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return {};
        value = (value << 4) | static_cast<uint64_t>(digit);
    }
    value &= ~static_cast<uint64_t>(0x1FFF);
    char out[17];
    std::snprintf(out, sizeof(out), "%016llX",
                  static_cast<unsigned long long>(value));
    // Los ids se comparan en minusculas en el resto del catalogo.
    for (char& c : out)
        if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
    return std::string(out, 16);
}

bool isBaseTitleId(const std::string& titleId) {
    // Comparar los tres ultimos CARACTERES no vale: un DLC es ...1000 y
    // tambien acaba en "000". El discriminante son los trece BITS bajos, asi
    // que se compara el id con su propia forma base.
    const std::string base = baseTitleIdOf(titleId);
    if (base.empty())
        return true;              // no clasificable: se muestra
    std::string lowered = titleId;
    for (char& c : lowered)
        if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
    return lowered == base;
}



bool isUpdateTitleId(const std::string& titleId) {
    if (titleId.size() != 16)
        return false;
    uint64_t value = 0;
    for (char c : titleId) {
        int digit;
        if (c >= '0' && c <= '9')      digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        value = (value << 4) | static_cast<uint64_t>(digit);
    }
    return (value & 0x1FFF) == 0x800;
}

bool isDlcTitleId(const std::string& titleId) {
    if (titleId.size() != 16)
        return false;
    uint64_t value = 0;
    for (char c : titleId) {
        int digit;
        if (c >= '0' && c <= '9')      digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        value = (value << 4) | static_cast<uint64_t>(digit);
    }
    return (value & 0x1FFF) >= 0x1000;
}

const char* const kSourceLabel = "GameHub";

namespace {

// Percent-decoding. File names travel URL-encoded inside the index, and the
// title shown to the user comes from the name, so it has to be readable again.
// Invalid escapes are left verbatim rather than dropped: a name is better shown
// slightly wrong than silently truncated.
std::string urlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size() &&
            std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
            const std::string hex = in.substr(i + 1, 2);
            out.push_back(static_cast<char>(std::strtol(hex.c_str(), nullptr, 16)));
            i += 2;
        } else if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

// Last path segment of a URL, query string stripped.
std::string fileNameOf(const std::string& url) {
    const size_t q = url.find_first_of("?#");
    const std::string path = q == std::string::npos ? url : url.substr(0, q);
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// A Nintendo Title ID is 16 hex chars, conventionally in brackets in the name.
// Accept it bare too: the brackets are a naming convention, not a guarantee.
std::string titleIdOf(const std::string& name) {
    for (size_t i = 0; i + 16 <= name.size(); ++i) {
        size_t n = 0;
        while (n < 16 && std::isxdigit(static_cast<unsigned char>(name[i + n]))) ++n;
        if (n < 16) { i += n; continue; }
        // Reject a longer hex run: that is a hash, not a Title ID.
        if (i + 16 < name.size() &&
            std::isxdigit(static_cast<unsigned char>(name[i + 16]))) { i += n; continue; }
        std::string id = name.substr(i, 16);
        for (char& c : id) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return id;
    }
    return {};
}

// Stable synthetic identity. No torrent exists, but downloads, dedup and the
// persisted queue are all keyed by info-hash, so entries need one. The URL is
// the natural source: unique per file and unchanged across refreshes.
std::string synthesizeInfoHash(const std::string& url) {
    uint8_t digest[20];
    sha1(url.data(), url.size(), digest);
    char hex[41];
    for (int i = 0; i < 20; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    hex[40] = '\0';
    return std::string(hex);
}

// scheme://host[:port] of a URL, lower-cased. Empty when it is not absolute.
std::string originOf(const std::string& url) {
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos) return {};
    const size_t hostStart = scheme + 3;
    const size_t hostEnd = url.find('/', hostStart);
    std::string origin =
        hostEnd == std::string::npos ? url : url.substr(0, hostEnd);
    for (char& c : origin) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return origin;
}

// Reads a JSON value as a string whatever its underlying type. The index is
// produced by a web app where a number may arrive quoted or bare.
std::string asString(const nlohmann::json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<int64_t>());
    if (v.is_number_unsigned()) return std::to_string(v.get<uint64_t>());
    return {};
}

} // namespace

std::string fileNameFromUrl(const std::string& url) {
    return urlDecode(fileNameOf(url));
}

bool isTrustedUrl(const std::string& url) {
    const std::string want = originOf(kCatalogUrl);
    return !want.empty() && originOf(url) == want;
}

bool parseCoverAspect(const std::string& json, int& width, int& height) {
    nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object())
        return false;
    const auto w = root.find("gh_cover_width");
    const auto h = root.find("gh_cover_height");
    if (w == root.end() || h == root.end() ||
        !w->is_number_integer() || !h->is_number_integer())
        return false;
    const int64_t wv = w->get<int64_t>();
    const int64_t hv = h->get<int64_t>();
    // Un rango sano: fuera de el, el indice miente o esta corrupto y es mejor
    // el valor por defecto que una tarjeta de un pixel.
    if (wv < 16 || hv < 16 || wv > 4096 || hv > 4096)
        return false;
    width  = static_cast<int>(wv);
    height = static_cast<int>(hv);
    return true;
}

bool parsePlatforms(const std::string& json, std::vector<ShopPlatform>& out,
                    std::string& error) {
    nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        error = "gamehub: payload is not a JSON object";
        return false;
    }
    const auto filesIt = root.find("files");
    if (filesIt == root.end() || !filesIt->is_array()) {
        error = "gamehub: platform list has no 'files' array";
        return false;
    }

    for (const auto& f : *filesIt) {
        if (!f.is_object()) continue;
        const auto slugIt = f.find("slug");
        // Sin slug no hay nada que pedir despues, asi que la fila no sirve de
        // nada. Se salta, como cualquier otra mal formada.
        if (slugIt == f.end() || !slugIt->is_string()) continue;

        ShopPlatform p;
        p.slug = slugIt->get<std::string>();
        if (p.slug.empty()) continue;

        const auto nameIt = f.find("name");
        // Sin nombre se ensena el slug: peor rotulo, pero la plataforma sigue
        // siendo utilizable, que es lo que importa.
        p.name = nameIt != f.end() && nameIt->is_string()
            ? nameIt->get<std::string>() : p.slug;

        const auto extIt = f.find("extensions");
        if (extIt != f.end() && extIt->is_array())
            for (const auto& e : *extIt)
                if (e.is_string()) p.extensions.push_back(e.get<std::string>());

        const auto gamesIt = f.find("games");
        if (gamesIt != f.end() && gamesIt->is_number_integer())
            p.games = gamesIt->get<int>();

        const auto nativeIt = f.find("native");
        if (nativeIt != f.end() && nativeIt->is_boolean())
            p.native = nativeIt->get<bool>();

        const auto emuIt = f.find("emulator");
        if (emuIt != f.end() && emuIt->is_string())
            p.emulator = emuIt->get<std::string>();

        // Mismo filtro que las caratulas: solo del servidor del usuario. Un
        // indice manipulado no va a hacer que la consola pida imagenes a
        // cualquier sitio.
        const auto iconIt = f.find("gh_icon");
        if (iconIt != f.end() && iconIt->is_string()) {
            const std::string icon = iconIt->get<std::string>();
            if (isTrustedUrl(icon))
                p.iconUrl = icon;
        }

        out.push_back(std::move(p));
    }
    return true;
}

bool parseGameDetail(const std::string& json, ShopGameDetail& out,
                     std::string& error) {
    nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        error = "gamehub: game detail is not valid JSON";
        return false;
    }
    auto str = [&root](const char* key) -> std::string {
        const auto it = root.find(key);
        return it != root.end() && it->is_string() ? it->get<std::string>()
                                                   : std::string();
    };


    out = ShopGameDetail{};
    const auto idIt = root.find("gh_id");
    if (idIt != root.end() && idIt->is_number_integer())
        out.id = idIt->get<int>();
    out.platformSlug = str("gh_platform");
    out.platformName = str("gh_platform_name");
    out.title = str("gh_title");
    out.description = str("gh_description");
    out.developer = str("gh_developer");
    out.publisher = str("gh_publisher");
    out.genre = str("gh_genre");
    out.region = str("gh_region");
    out.languages = str("gh_languages");

    const auto yearIt = root.find("gh_year");
    if (yearIt != root.end() && yearIt->is_number_integer())
        out.year = std::to_string(yearIt->get<int>());
    else
        out.year = str("gh_year");

    // Portada y capturas SOLO del servidor del usuario. Una ficha manipulada no
    // va a hacer que la consola pida imagenes a cualquier sitio; es el mismo
    // filtro que ya llevan el catalogo y los iconos de plataforma.
    const std::string cover = str("gh_cover");
    if (isTrustedUrl(cover))
        out.coverUrl = cover;

    const auto shotsIt = root.find("gh_screenshots");
    if (shotsIt != root.end() && shotsIt->is_array()) {
        for (const auto& shot : *shotsIt) {
            if (!shot.is_string())
                continue;
            const std::string url = shot.get<std::string>();
            if (isTrustedUrl(url))
                out.screenshots.push_back(url);
        }
    }
    // El trailer NO se filtra por host: es un enlace de YouTube y no lo abre la
    // consola, solo se ensena para verlo en otro sitio.
    out.trailerUrl = str("gh_trailer");

    const auto filesIt = root.find("files");
    if (filesIt != root.end() && filesIt->is_array()) {
        for (const auto& f : *filesIt) {
            if (!f.is_object())
                continue;
            const auto urlIt = f.find("url");
            if (urlIt == f.end() || !urlIt->is_string())
                continue;
            ShopGameFile file;
            file.url = urlIt->get<std::string>();
            // Una descarga que no sale de nuestro servidor no se ofrece.
            if (!isTrustedUrl(file.url))
                continue;
            const auto sizeIt = f.find("size");
            if (sizeIt != f.end() && sizeIt->is_number_unsigned())
                file.size = sizeIt->get<uint64_t>();
            const auto kindIt = f.find("gh_kind");
            if (kindIt != f.end() && kindIt->is_string())
                file.kind = kindIt->get<std::string>();
            const auto titleIt = f.find("gh_title");
            if (titleIt != f.end() && titleIt->is_string())
                file.title = titleIt->get<std::string>();
            const auto regionIt = f.find("gh_region");
            if (regionIt != f.end() && regionIt->is_string())
                file.region = regionIt->get<std::string>();
            const auto langIt = f.find("gh_languages");
            if (langIt != f.end() && langIt->is_string())
                file.languages = langIt->get<std::string>();
            out.files.push_back(std::move(file));
        }
    }

    // Sin nada que descargar la ficha no sirve para lo que existe. Es distinto
    // de un campo suelto que falte, que se aguanta perfectamente.
    if (out.files.empty()) {
        error = "gamehub: game detail has no downloadable file";
        return false;
    }
    return true;
}

bool parsePlatformIndex(const std::string& json, const std::string& slug,
                        std::vector<CatalogEntry>& out, std::string& error) {
    if (slug.empty()) {
        error = "gamehub: platform index needs a slug";
        return false;
    }
    const size_t before = out.size();
    // Los sub-indices son cosa de Switch --actualizaciones y DLC-- y una
    // plataforma emulada no los tiene, asi que lo que venga se descarta.
    std::vector<std::string> ignoredSubIndexes;
    if (!parseCatalog(json, out, ignoredSubIndexes, error))
        return false;

    for (size_t i = before; i < out.size(); ++i)
        out[i].platformSlug = slug;
    return true;
}

bool parseCatalog(const std::string& json, std::vector<CatalogEntry>& out,
                  std::vector<std::string>& subIndexes, std::string& error) {
    nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        error = "gamehub: payload is not a JSON object";
        return false;
    }

    const auto filesIt = root.find("files");
    if (filesIt == root.end() || !filesIt->is_array()) {
        error = "gamehub: index has no 'files' array";
        return false;
    }

    // titledb is optional; without it entries still work, they just show the
    // file name instead of a real title and carry no cover.
    const nlohmann::json* titledb = nullptr;
    const auto tdbIt = root.find("titledb");
    if (tdbIt != root.end() && tdbIt->is_object()) titledb = &(*tdbIt);

    for (const auto& f : *filesIt) {
        if (!f.is_object()) continue;
        const auto urlIt = f.find("url");
        if (urlIt == f.end() || !urlIt->is_string()) continue;
        const std::string url = urlIt->get<std::string>();
        if (url.empty()) continue;

        CatalogEntry e;
        e.directUrl = url;
        e.infoHash  = synthesizeInfoHash(url);

        const auto sizeIt = f.find("size");
        if (sizeIt != f.end() && sizeIt->is_number()) {
            const int64_t s = sizeIt->get<int64_t>();
            if (s > 0) e.size = static_cast<uint64_t>(s);
        }

        const std::string name = urlDecode(fileNameOf(url));
        e.title   = name;
        e.titleId = titleIdOf(name);

        // ── Claves gh_ por fichero ──────────────────────────────────────────
        // El titledb va indexado por Title ID, que sale del nombre del fichero:
        // un volcado mal nombrado se quedaba sin entrada y por tanto sin
        // portada, aunque el servidor la tuviera guardada. Estas claves viajan
        // en la propia fila de files[] y no dependen de ese id.
        if (const auto it = f.find("gh_id");
            it != f.end() && it->is_number_integer())
            e.gameId = it->get<int64_t>();
        if (const auto it = f.find("gh_title"); it != f.end()) {
            const std::string t = asString(*it);
            if (!t.empty()) e.title = t;
        }
        if (const auto it = f.find("gh_cover"); it != f.end()) {
            const std::string u = asString(*it);
            if (!u.empty() && isTrustedUrl(u)) e.posterUrl = u;
        }
        // gh_kind ("dlc"/"update") no se guarda: la procedencia --de que indice
        // vino-- ya dice que es un complemento, y el Title ID distingue el tipo
        // cuando hace falta. Se documenta aqui para que nadie lo busque.

        // Rich metadata when the index carries it for this Title ID.
        if (titledb && !e.titleId.empty()) {
            const auto m = titledb->find(e.titleId);
            if (m != titledb->end() && m->is_object()) {
                const auto get = [&m](const char* k) -> const nlohmann::json* {
                    const auto it = m->find(k);
                    return it == m->end() ? nullptr : &(*it);
                };
                if (const auto* v = get("name")) {
                    const std::string s = asString(*v);
                    if (!s.empty()) e.title = s;
                }
                if (const auto* v = get("description")) e.description = asString(*v);
                if (const auto* v = get("publisher"))   e.publisher   = asString(*v);
                if (const auto* v = get("iconUrl")) {
                    const std::string s = asString(*v);
                    // Only trust cover URLs from the same server; the index is
                    // ours, but a field is still a field.
                    if (!s.empty() && isTrustedUrl(s)) e.posterUrl = s;
                }
                // releaseDate is YYYYMMDD as an int — only the year is real,
                // the day is a placeholder the shop fills in.
                if (const auto* v = get("releaseDate")) {
                    const std::string s = asString(*v);
                    if (s.size() >= 4) e.year = s.substr(0, 4);
                }
                // category is a list; the facts table shows a single genre.
                if (const auto* v = get("category")) {
                    if (v->is_array() && !v->empty()) e.genre = asString(v->front());
                    else e.genre = asString(*v);
                }
                if (const auto* v = get("size")) {
                    if (e.size == 0 && v->is_number()) {
                        const int64_t s = v->get<int64_t>();
                        if (s > 0) e.size = static_cast<uint64_t>(s);
                    }
                }

                // ── Claves gh_: la extension propia de nuestra tienda ────────
                // El titledb de Tinfoil no tiene sitio para region, idiomas,
                // desarrollador ni capturas, asi que GameHub las publica aparte.
                // Cuando faltan (indice de otra tienda) la ficha simplemente
                // muestra menos, nunca falla.
                if (const auto* v = get("gh_title")) {
                    const std::string t = asString(*v);
                    // gh_title viene sin la region pegada; `name` la lleva
                    // dentro porque Tinfoil solo dispone de ese campo.
                    if (!t.empty()) e.title = t;
                }
                if (const auto* v = get("gh_region"))    e.region     = asString(*v);
                if (const auto* v = get("gh_languages")) e.languages  = asString(*v);
                if (const auto* v = get("gh_developer")) e.developer  = asString(*v);
                if (const auto* v = get("gh_group"))     e.groupKey   = asString(*v);
                if (const auto* v = get("gh_year")) {
                    const std::string y = asString(*v);
                    if (!y.empty()) e.year = y;   // mas fiable que el releaseDate sintetico
                }
                if (const auto* v = get("gh_trailer")) {
                    // No se comprueba isTrustedUrl: el trailer apunta a YouTube
                    // a proposito y no se descarga, solo se muestra como codigo
                    // QR para abrirlo en el movil.
                    e.trailerUrl = asString(*v);
                }
                // gh_cover antes que iconUrl: la primera viene en JPEG y la
                // segunda en WebP, que stb_image no sabe decodificar.
                if (const auto* v = get("gh_cover")) {
                    const std::string u = asString(*v);
                    if (!u.empty() && isTrustedUrl(u))
                        e.posterUrl = u;
                }
                if (const auto* v = get("gh_screenshots")) {
                    if (v->is_array()) {
                        for (const auto& sc : *v) {
                            const std::string u = asString(sc);
                            // Las capturas SI se descargan y se pintan, asi que
                            // solo se aceptan las del propio servidor.
                            if (!u.empty() && isTrustedUrl(u)) e.screenshots.push_back(u);
                        }
                    }
                }

                e.metadataOk = true;
            }
        }

        // A file served by our own server has no swarm whose health could be in
        // doubt: it is either there or the fetch fails outright.
        e.health = CatalogHealth::Ok;
        out.push_back(std::move(e));
    }

    // Sub-indexes (DLC, updates) are announced by the payload, not assumed.
    const auto dirsIt = root.find("directories");
    if (dirsIt != root.end() && dirsIt->is_array()) {
        for (const auto& d : *dirsIt) {
            if (!d.is_string()) continue;
            const std::string u = d.get<std::string>();
            if (!u.empty() && isTrustedUrl(u)) subIndexes.push_back(u);
        }
    }

    return true;
}

} // namespace gamehub
} // namespace gamehubnx
