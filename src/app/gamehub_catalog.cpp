#include "gamehub_catalog.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

// sha1.h is plain C and carries no extern "C" guard of its own, so the
// declarations must be wrapped here or C++ name mangling loses them at link.
extern "C" {
#include "../core/sha1.h"
}

namespace pipensx {
namespace gamehub {

const char* const kCatalogUrl  = "https://gamehub.hdglabs.com/api/shop";
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

bool isTrustedUrl(const std::string& url) {
    const std::string want = originOf(kCatalogUrl);
    return !want.empty() && originOf(url) == want;
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
} // namespace pipensx
