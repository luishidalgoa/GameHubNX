#include "homebrew_source.hpp"

#include "app/curl_https.hpp"
#include "app/file_replace.hpp"
#include "app/zip_archive.hpp"
#include "core/md5.h"

#include <borealis/extern/nlohmann/json.hpp>

#include <curl/curl.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#ifndef GAMEHUBNX_VERSION
#define GAMEHUBNX_VERSION "0.0.0"
#endif

namespace gamehubnx {
namespace {

// Repos de serie: los tres proyectos homebrew que publican su .nro en GitHub.
constexpr const char* kDefaultRepos[] = {
    "NextendoNetwork/Prelude-Nextendo",
    "luishidalgoa/green-nx",
    "ticohq/tico",
};

// La lista de releases de un repo cabe de sobra en esto: 30 por pagina.
constexpr size_t kReleasesLimit = 512 * 1024;
constexpr size_t kNroLimit = 128 * 1024 * 1024;
constexpr const char* kVersionFile = ".gamehubnx-version";

// GitHub manda null en campos como body o published_at. value() lanza con null,
// asi que cada campo se lee con su tipo comprobado y cae a un valor por defecto.
std::string textField(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>()
                                                 : std::string();
}

bool flagField(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() && it->get<bool>();
}

uint64_t sizeField(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number_unsigned()
               ? it->get<uint64_t>()
               : uint64_t{0};
}

std::string stripV(const std::string& tag) {
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V'))
        return tag.substr(1);
    return tag;
}

// Solo los digitos de un segmento ("3-beta" -> 3). Sin digitos, cero.
uint64_t segmentValue(const std::string& segment) {
    return std::strtoull(segment.c_str(), nullptr, 10);
}

// Trozos separados por puntos, sin lo que venga tras un guion o un espacio.
std::vector<std::string> versionSegments(const std::string& version) {
    std::string core = version.substr(0, version.find_first_of("- "));
    std::vector<std::string> segments;
    std::string current;
    for (char c : core) {
        if (c == '.') {
            segments.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    segments.push_back(current);
    return segments;
}

struct HttpBuffer {
    std::string data;
    size_t limit = 0;
    bool overflow = false;
};

size_t appendToBuffer(char* bytes, size_t size, size_t count, void* opaque) {
    auto* buffer = static_cast<HttpBuffer*>(opaque);
    const size_t received = size * count;
    if (buffer->data.size() + received > buffer->limit) {
        buffer->overflow = true;
        return 0;
    }
    buffer->data.append(bytes, received);
    return received;
}

// Mensaje de una descarga que no llego a buen puerto. Lleva el codigo de curl,
// el HTTP y los bytes recibidos: "se ha interrumpido" a secas no dice si fallo
// la red, el servidor o la SD.
std::string transferFailure(CURLcode result, long status, size_t received) {
    return std::string("La descarga se ha interrumpido (curl ") +
           std::to_string(static_cast<int>(result)) + ": " +
           curl_easy_strerror(result) + ", HTTP " + std::to_string(status) +
           ", " + std::to_string(received) + " bytes).";
}

struct FileSink {
    std::ofstream output;
    size_t written = 0;
    size_t limit = 0;
    bool overflow = false;
};

size_t writeToFile(char* bytes, size_t size, size_t count, void* opaque) {
    auto* sink = static_cast<FileSink*>(opaque);
    const size_t received = size * count;
    if (sink->written + received > sink->limit) {
        sink->overflow = true;
        return 0;
    }
    sink->output.write(bytes, static_cast<std::streamsize>(received));
    if (!sink->output.good())
        return 0;
    sink->written += received;
    return received;
}

void configureCommon(CURL* curl, const std::string& url) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "GameHubNX/" GAMEHUBNX_VERSION);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curlPinHttpsOnly(curl);
    curlApplyTrustedSsl(curl);
}

// Crea la ruta entera, nivel a nivel. Si ya existe, no pasa nada.
bool makeDirectories(const std::string& path) {
    for (size_t slash = path.find('/', 1); slash != std::string::npos;
         slash = path.find('/', slash + 1)) {
        const std::string part = path.substr(0, slash);
        if (mkdir(part.c_str(), 0777) != 0 && errno != EEXIST)
            return false;
    }
    if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST)
        return false;
    return true;
}

std::string lowerCopy(std::string text) {
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool endsWithNro(const std::string& name) {
    const std::string lower = lowerCopy(name);
    return lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".nro") == 0;
}

} // namespace

const HomebrewRelease* HomebrewApp::recommended() const {
    for (const HomebrewRelease& release : releases)
        if (!release.prerelease)
            return &release;
    return releases.empty() ? nullptr : &releases.front();
}

int compareHomebrewVersions(const std::string& a, const std::string& b) {
    const std::vector<std::string> left = versionSegments(stripV(a));
    const std::vector<std::string> right = versionSegments(stripV(b));
    const size_t count = left.size() > right.size() ? left.size() : right.size();
    for (size_t i = 0; i < count; ++i) {
        const uint64_t l = i < left.size() ? segmentValue(left[i]) : 0;
        const uint64_t r = i < right.size() ? segmentValue(right[i]) : 0;
        if (l != r)
            return l < r ? -1 : 1;
    }
    return 0;
}

bool parseGithubReleases(const std::string& json,
                         std::vector<HomebrewRelease>& out,
                         std::string& error) {
    out.clear();
    nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_array()) {
        error = "La respuesta de GitHub no es una lista de releases.";
        return false;
    }
    for (const nlohmann::json& release : root) {
        if (!release.is_object() || flagField(release, "draft"))
            continue;
        if (!release.contains("assets") || !release["assets"].is_array())
            continue;
        // Un release sin .nro no es una app que podamos instalar: se omite.
        const nlohmann::json* nro = nullptr;
        for (const nlohmann::json& asset : release["assets"]) {
            if (asset.is_object() && endsWithNro(textField(asset, "name")) &&
                asset.contains("browser_download_url")) {
                nro = &asset;
                break;
            }
        }
        if (!nro)
            continue;

        HomebrewRelease entry;
        entry.tag = textField(release, "tag_name");
        if (entry.tag.empty())
            continue;
        entry.version = stripV(entry.tag);
        entry.nroName = textField(*nro, "name");
        entry.nroUrl = textField(*nro, "browser_download_url");
        entry.nroSize = sizeField(*nro, "size");
        entry.publishedAt = textField(release, "published_at");
        entry.notes = textField(release, "body");
        entry.prerelease = flagField(release, "prerelease");
        out.push_back(std::move(entry));
    }
    error.clear();
    return true;
}

std::vector<std::string> defaultHomebrewRepos() {
    std::vector<std::string> repos;
    for (const char* repo : kDefaultRepos)
        repos.emplace_back(repo);
    return repos;
}

bool downloadGithubReleases(const std::string& repo, std::string& body,
                            std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "No se pudo iniciar el cliente HTTP.";
        return false;
    }
    const std::string url =
        "https://api.github.com/repos/" + repo + "/releases?per_page=30";
    configureCommon(curl, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    struct curl_slist* headers =
        curl_slist_append(nullptr, "Accept: application/vnd.github+json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    HttpBuffer buffer;
    buffer.limit = kReleasesLimit;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToBuffer);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        error = buffer.overflow ? "La lista de releases es demasiado grande."
                                : "Sin conexion con GitHub.";
        return false;
    }
    // 403 con el limite agotado: GitHub lo dice asi en la cabecera de la
    // respuesta, pero el codigo basta para avisar sin mas.
    if (status == 403 || status == 429) {
        error = "GitHub ha limitado las peticiones. Vuelve a probar mas tarde.";
        return false;
    }
    if (status == 404) {
        error = "Repositorio no encontrado: " + repo;
        return false;
    }
    if (status != 200) {
        error = "GitHub respondio con el codigo " + std::to_string(status) + ".";
        return false;
    }
    body = std::move(buffer.data);
    error.clear();
    return true;
}

bool fetchGithubReleases(const std::string& repo, HomebrewApp& app,
                         std::string& error) {
    app.repo = repo;
    app.pageUrl = "https://github.com/" + repo;
    app.releases.clear();
    std::string body;
    if (!downloadGithubReleases(repo, body, error))
        return false;
    return parseGithubReleases(body, app.releases, error);
}

std::string homebrewNroPath(const HomebrewRelease& release) {
    if (!release.binaryPath.empty())
        return "sdmc:" + release.binaryPath;
    std::string stem = release.nroName;
    if (endsWithNro(stem))
        stem = stem.substr(0, stem.size() - 4);
    return "sdmc:/switch/" + stem + "/" + release.nroName;
}

std::string installedHomebrewVersion(const std::string& nroPath) {
    const size_t slash = nroPath.find_last_of('/');
    if (slash == std::string::npos)
        return {};
    std::ifstream in(nroPath.substr(0, slash + 1) + kVersionFile);
    std::string version;
    if (in)
        std::getline(in, version);
    return version;
}

static bool installArchiveRelease(const HomebrewRelease& release,
                                   std::string& installedPath,
                                   std::string& error);

bool installHomebrewRelease(const HomebrewRelease& release,
                            std::string& installedPath, std::string& error) {
    if (release.isArchive())
        return installArchiveRelease(release, installedPath, error);
    installedPath = homebrewNroPath(release);
    const std::string dir =
        installedPath.substr(0, installedPath.find_last_of('/'));
    if (!makeDirectories(dir)) {
        error = "No se pudo crear " + dir + ".";
        return false;
    }

    const std::string partial = installedPath + ".part";
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "No se pudo iniciar el cliente HTTP.";
        return false;
    }
    configureCommon(curl, release.nroUrl);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L * 60L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    curlTuneDownloadSocket(curl);

    FileSink sink;
    sink.output.open(partial, std::ios::binary | std::ios::trunc);
    sink.limit = kNroLimit;
    if (!sink.output) {
        curl_easy_cleanup(curl);
        error = "No se pudo escribir en " + partial + ".";
        return false;
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    sink.output.close();

    if (result != CURLE_OK || status != 200 || sink.overflow ||
        (release.nroSize && sink.written != release.nroSize)) {
        std::remove(partial.c_str());
        error = result != CURLE_OK
                    ? transferFailure(result, status, sink.written)
                    : std::string("La descarga no coincide con el release (HTTP ") +
                          std::to_string(status) + ", " +
                          std::to_string(sink.written) + " bytes).";
        return false;
    }
    if (!replaceFile(partial, installedPath)) {
        std::remove(partial.c_str());
        error = "No se pudo dejar el .nro en su sitio.";
        return false;
    }
    // La version se apunta al final: si falla aqui, el .nro ya esta bien y la
    // app solo lo mostrara como instalado sin version.
    std::ofstream version(dir + "/" + kVersionFile, std::ios::trunc);
    version << release.tag << '\n';
    error.clear();
    return true;
}

namespace {

// El repo.json de HB App Store y sus zips. Se descargan enteros al abrir la
// pestana: son unos 1,6 MB, un limite de 16 MB deja margen de sobra.
constexpr size_t kHbRepoLimit = 16 * 1024 * 1024;
constexpr size_t kArchiveLimit = 256 * 1024 * 1024;
constexpr const char* kHbSwitchRepo = "https://switch.cdn.fortheusers.org";
// Carpeta de trabajo para los zips antes de extraerlos. Vive dentro de nuestro
// directorio, asi que un corte deja basura que el usuario puede borrar.
constexpr const char* kStageDir = "sdmc:/switch/gamehubnx/tmp";

bool startsWith(const std::string& text, const char* prefix) {
    return text.compare(0, std::strlen(prefix), prefix) == 0;
}

// Un nombre de paquete o de fichero que se usa como parte de una ruta: sin
// barras, sin "..", sin caracteres de control.
bool isSafeComponent(const std::string& name) {
    if (name.empty() || name == "." || name == "..")
        return false;
    for (unsigned char c : name)
        if (c == '/' || c == '\\' || c < 0x20 || c == 0x7f)
            return false;
    return true;
}

// Las rutas de binary llegan con "/" inicial: "/switch/app/app.nro". Solo se
// aceptan dentro de switch/ y terminadas en .nro.
bool isSafeBinaryPath(const std::string& binary) {
    if (!startsWith(binary, "/switch/"))
        return false;
    const std::string lower = lowerCopy(binary);
    if (lower.size() < 5 || lower.compare(lower.size() - 4, 4, ".nro") != 0)
        return false;
    return isSafeZipPath(binary.substr(1));
}

std::string lastSegment(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

struct ArchiveSink {
    std::ofstream output;
    md5_ctx_t md5;
    size_t written = 0;
    size_t limit = 0;
    bool overflow = false;
};

// Escribe el zip y calcula su md5 sobre la marcha: no hay que releer 5 MB.
size_t writeArchive(char* bytes, size_t size, size_t count, void* opaque) {
    auto* sink = static_cast<ArchiveSink*>(opaque);
    const size_t received = size * count;
    if (sink->written + received > sink->limit) {
        sink->overflow = true;
        return 0;
    }
    sink->output.write(bytes, static_cast<std::streamsize>(received));
    if (!sink->output.good())
        return 0;
    md5_update(&sink->md5, bytes, received);
    sink->written += received;
    return received;
}

bool sameHexDigest(const std::string& a, const std::string& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// Extrae la parte de un paquete que va a la SD. manifest.install e info.json son
// metadatos de libget, no ficheros de la consola. Solo se extrae switch/ y
// config/: lo demas del zip no es de Switch y no se escribe.
bool extractArchiveEntries(const std::string& zipPath, std::string& error) {
    ZipArchive zip;
    if (!zip.open(zipPath, error))
        return false;
    for (const ZipEntry& entry : zip.entries()) {
        if (!startsWith(entry.name, "switch/") &&
            !startsWith(entry.name, "config/"))
            continue;
        if (!zip.extract(entry, std::string("sdmc:/") + entry.name, error))
            return false;
    }
    return true;
}

} // namespace

std::string homebrewSwitchRepoUrl() { return kHbSwitchRepo; }

bool parseHbRepo(const std::string& json, const std::string& baseUrl,
                 std::vector<HomebrewApp>& out, std::string& error) {
    out.clear();
    nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("packages") ||
        !root["packages"].is_array()) {
        error = "El repo.json de HB App Store no tiene la forma esperada.";
        return false;
    }
    const std::string base = baseUrl.empty() || baseUrl.back() != '/'
                                 ? baseUrl
                                 : baseUrl.substr(0, baseUrl.size() - 1);
    for (const nlohmann::json& package : root["packages"]) {
        if (!package.is_object())
            continue;
        const std::string name = textField(package, "name");
        const std::string binary = textField(package, "binary");
        // Sin nombre seguro o sin ruta del .nro no hay donde instalarlo.
        if (!isSafeComponent(name) || !isSafeBinaryPath(binary))
            continue;

        HomebrewApp app;
        app.repo = name;
        app.title = textField(package, "title");
        app.author = textField(package, "author");
        app.description = textField(package, "description");
        app.pageUrl = textField(package, "url");

        HomebrewRelease release;
        release.tag = textField(package, "version");
        release.version = stripV(release.tag);
        if (release.tag.empty())
            continue;
        release.binaryPath = binary;
        release.nroName = lastSegment(binary);
        release.archiveUrl = base + "/zips/" + name + ".zip";
        release.archiveMd5 = textField(package, "md5");
        app.releases.push_back(std::move(release));
        out.push_back(std::move(app));
    }
    error.clear();
    return true;
}

bool downloadHbRepo(const std::string& baseUrl, std::string& body,
                    std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "No se pudo iniciar el cliente HTTP.";
        return false;
    }
    const std::string url = baseUrl + "/repo.json";
    configureCommon(curl, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    HttpBuffer buffer;
    buffer.limit = kHbRepoLimit;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToBuffer);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        error = buffer.overflow ? "El repo.json de HB App Store es demasiado grande."
                                : "Sin conexion con HB App Store.";
        return false;
    }
    if (status != 200) {
        error = "HB App Store respondio con el codigo " + std::to_string(status) + ".";
        return false;
    }
    body = std::move(buffer.data);
    error.clear();
    return true;
}

bool fetchHbRepo(const std::string& baseUrl, std::vector<HomebrewApp>& out,
                 std::string& error) {
    std::string body;
    if (!downloadHbRepo(baseUrl, body, error))
        return false;
    return parseHbRepo(body, baseUrl, out, error);
}

// Descarga el zip del paquete, comprueba su md5 con el que publica el repo, lo
// extrae en la SD y deja la version junto al .nro. Si algo falla, no se queda
// ningun zip ni ningun fichero a medias en la tarjeta.
static bool installArchiveRelease(const HomebrewRelease& release,
                                   std::string& installedPath,
                                   std::string& error) {
    installedPath = homebrewNroPath(release);
    if (!makeDirectories(kStageDir)) {
        error = "No se pudo crear la carpeta temporal.";
        return false;
    }
    const std::string zipPath = std::string(kStageDir) + "/" +
                                lastSegment(release.archiveUrl);

    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "No se pudo iniciar el cliente HTTP.";
        return false;
    }
    configureCommon(curl, release.archiveUrl);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L * 60L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    curlTuneDownloadSocket(curl);

    ArchiveSink sink;
    sink.output.open(zipPath, std::ios::binary | std::ios::trunc);
    sink.limit = kArchiveLimit;
    md5_init(&sink.md5);
    if (!sink.output) {
        curl_easy_cleanup(curl);
        error = "No se pudo escribir en la carpeta temporal.";
        return false;
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeArchive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    sink.output.close();

    if (result != CURLE_OK || status != 200 || sink.overflow) {
        std::remove(zipPath.c_str());
        error = result != CURLE_OK
                    ? transferFailure(result, status, sink.written)
                    : std::string("La descarga no se completo (HTTP ") +
                          std::to_string(status) + ", " +
                          std::to_string(sink.written) + " bytes).";
        return false;
    }
    uint8_t digest[16];
    md5_final(&sink.md5, digest);
    char hex[33];
    for (int i = 0; i < 16; ++i)
        std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    if (!release.archiveMd5.empty() && !sameHexDigest(hex, release.archiveMd5)) {
        std::remove(zipPath.c_str());
        error = "El zip no coincide con el md5 publicado.";
        return false;
    }

    const bool extracted = extractArchiveEntries(zipPath, error);
    std::remove(zipPath.c_str());
    if (!extracted) {
        if (error.empty())
            error = "No se pudo extraer el paquete.";
        return false;
    }
    const std::string dir = installedPath.substr(0, installedPath.find_last_of('/'));
    std::ofstream version(dir + "/" + kVersionFile, std::ios::trunc);
    version << release.tag << '\n';
    error.clear();
    return true;
}

namespace {

constexpr size_t kRepoListLimit = 64 * 1024;
constexpr const char* kHomebrewReposUrl = "https://gamehub.hdglabs.com/api/shop/homebrew";

// Mismo criterio que la ruta de GameHub: un nombre de repo de GitHub, sin
// barras extra ni espacios ni caracteres raros.
bool isValidRepoName(const std::string& repo) {
    const size_t slash = repo.find('/');
    if (slash == std::string::npos || repo.find('/', slash + 1) != std::string::npos)
        return false;
    const auto segment = [](const std::string& part) {
        if (part.empty() || part == "." || part == "..")
            return false;
        for (unsigned char c : part)
            if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-'))
                return false;
        return true;
    };
    return segment(repo.substr(0, slash)) && segment(repo.substr(slash + 1));
}

} // namespace

std::string homebrewReposUrl() { return kHomebrewReposUrl; }

bool downloadHomebrewRepoList(std::string& body, std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "No se pudo iniciar el cliente HTTP.";
        return false;
    }
    configureCommon(curl, kHomebrewReposUrl);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    HttpBuffer buffer;
    buffer.limit = kRepoListLimit;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToBuffer);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        error = "Sin conexion con GameHub.";
        return false;
    }
    if (status != 200) {
        error = "GameHub respondio con el codigo " + std::to_string(status) + ".";
        return false;
    }
    body = std::move(buffer.data);
    error.clear();
    return true;
}

bool parseHomebrewRepoList(const std::string& json,
                           std::vector<std::string>& out, std::string& error) {
    out.clear();
    nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("repos") ||
        !root["repos"].is_array()) {
        error = "La lista de repos de GameHub no tiene la forma esperada.";
        return false;
    }
    for (const nlohmann::json& entry : root["repos"]) {
        if (!entry.is_object())
            continue;
        const std::string repo = textField(entry, "repo");
        if (!isValidRepoName(repo))
            continue;
        if (std::find(out.begin(), out.end(), repo) == out.end())
            out.push_back(repo);
    }
    error.clear();
    return true;
}

namespace {

struct ByteSink {
    std::vector<uint8_t>* out = nullptr;
    size_t limit = 0;
    bool overflow = false;
};

size_t appendBytes(char* bytes, size_t size, size_t count, void* opaque) {
    auto* sink = static_cast<ByteSink*>(opaque);
    const size_t received = size * count;
    if (sink->out->size() + received > sink->limit) {
        sink->overflow = true;
        return 0;
    }
    sink->out->insert(sink->out->end(), reinterpret_cast<uint8_t*>(bytes),
                      reinterpret_cast<uint8_t*>(bytes) + received);
    return received;
}

} // namespace

bool downloadBytes(const std::string& url, uint64_t offset, size_t length,
                   size_t limit, std::vector<uint8_t>& out, std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "No se pudo iniciar el cliente HTTP.";
        return false;
    }
    configureCommon(curl, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    if (length > 0) {
        const std::string range = std::to_string(offset) + "-" +
                                  std::to_string(offset + length - 1);
        curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
    }
    out.clear();
    ByteSink sink;
    sink.out = &out;
    sink.limit = length > 0 ? length : limit;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBytes);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK || sink.overflow) {
        out.clear();
        error = sink.overflow ? "La respuesta es mas grande de lo esperado."
                              : "Sin conexion.";
        return false;
    }
    // Con rango, un servidor que ignora la cabecera manda 200 y el recurso
    // entero: no lo aceptamos, seria bajarse el .nro completo.
    const bool ok = length > 0 ? status == 206 : status == 200;
    if (!ok) {
        out.clear();
        error = "Respuesta " + std::to_string(status) + " al pedir " + url + ".";
        return false;
    }
    error.clear();
    return true;
}

NroReader remoteNroReader(const std::string& url) {
    return [url](uint64_t offset, size_t length, std::vector<uint8_t>& out) {
        std::string error;
        return downloadBytes(url, offset, length, 0, out, error) &&
               out.size() == length;
    };
}



} // namespace gamehubnx
