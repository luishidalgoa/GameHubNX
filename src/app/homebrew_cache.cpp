#include "homebrew_cache.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <sys/stat.h>

namespace gamehubnx {
namespace {

constexpr const char* kCacheRoot = "sdmc:/switch/gamehubnx/homebrew-cache";
constexpr const char* kHbKey = "hb-switch";

// Crea la ruta entera, nivel a nivel. Si ya existe, no pasa nada. Salta el
// prefijo "sdmc:/" como en homebrew_icons.cpp: mkdir sobre la raiz del montaje
// falla en libnx y dejaba la cache sin escribir.
bool makeDirectories(const std::string& path) {
    size_t start = 1;
    const size_t colon = path.find(':');
    if (colon != std::string::npos && colon + 1 < path.size() && path[colon + 1] == '/')
        start = colon + 2;
    for (size_t slash = path.find('/', start); slash != std::string::npos;
         slash = path.find('/', slash + 1)) {
        const std::string part = path.substr(0, slash);
        if (mkdir(part.c_str(), 0777) != 0 && errno != EEXIST)
            return false;
    }
    if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST)
        return false;
    return true;
}

} // namespace

std::string homebrewCacheRoot() { return kCacheRoot; }

std::string homebrewCacheFile(const std::string& root, const std::string& key) {
    return root + "/" + key + ".json";
}

std::string homebrewCacheKeyForGithub(const std::string& repo) {
    std::string key = "gh-" + repo;
    for (char& c : key)
        if (c == '/')
            c = '_';
    return key;
}

std::string homebrewCacheKeyForHb() { return kHbKey; }
std::string homebrewCacheKeyForRepoList() { return "gamehub-repos"; }

bool readHomebrewCache(const std::string& path, HomebrewCacheEntry& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::string stamp;
    if (!std::getline(in, stamp))
        return false;
    std::stringstream rest;
    rest << in.rdbuf();
    out.fetchedAt = std::strtoll(stamp.c_str(), nullptr, 10);
    out.body = rest.str();
    return true;
}

bool writeHomebrewCache(const std::string& path, const HomebrewCacheEntry& entry,
                        std::string& error) {
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && !makeDirectories(path.substr(0, slash))) {
        error = "No se pudo crear la carpeta de la cache.";
        return false;
    }
    // Se escribe a un temporal y se renombra: una interrupcion no deja una
    // cache a medias que luego parezca valida.
    const std::string partial = path + ".part";
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "No se pudo escribir la cache.";
            return false;
        }
        out << entry.fetchedAt << '\n' << entry.body;
        if (!out.good()) {
            out.close();
            std::remove(partial.c_str());
            error = "No se pudo escribir la cache.";
            return false;
        }
    }
    if (std::rename(partial.c_str(), path.c_str()) != 0) {
        std::remove(partial.c_str());
        error = "No se pudo guardar la cache.";
        return false;
    }
    error.clear();
    return true;
}

bool homebrewCacheIsFresh(const HomebrewCacheEntry& entry, int64_t now,
                          int64_t ttl) {
    if (entry.fetchedAt <= 0 || now < entry.fetchedAt)
        return false;
    return now - entry.fetchedAt < ttl;
}

} // namespace gamehubnx
