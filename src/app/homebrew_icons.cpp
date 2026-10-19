#include "homebrew_icons.hpp"

#include "app/homebrew_source.hpp"
#include "app/nro_metadata.hpp"
#include "app/file_replace.hpp"
#include "app/rgba_image.hpp"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iterator>

#include <sys/stat.h>

#include <borealis/extern/nanovg/stb_image.h>

namespace gamehubnx {
namespace {

constexpr const char* kIconDir = "sdmc:/switch/gamehubnx/icons";
// Un icono de HB App Store pesa unos KB; cualquier cosa de mas no es un icono.
constexpr size_t kIconLimit = 256 * 1024;
// La casilla pinta el icono a 120 px. Un NRO trae 256x256 (256 KB en RGBA) y con
// cientos de apps en la cuadricula eso es demasiada memoria: se reduce aqui.
constexpr int kIconMaxSide = 128;

// Nombre de fichero para una clave de cache. La clave puede llevar rutas (las
// apps sin catalogo usan su .nro), y '/' o ':' no van en un nombre; un hash
// FNV-1a da uno seguro y estable.
std::string cacheFileName(const std::string& key) {
    uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char c : key) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.img",
                  static_cast<unsigned long long>(hash));
    return name;
}

bool readWhole(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}

bool writeWhole(const std::string& path, const std::vector<uint8_t>& data) {
    const std::string partial = path + ".part";
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        if (!out.good())
            return false;
    }
    return replaceFile(partial, path);
}

// Crea la ruta entera, nivel a nivel. Salta el prefijo "sdmc:/": mkdir sobre la
// raiz del montaje falla con un errno distinto de EEXIST en libnx, y eso cortaba
// el recorrido antes de crear nada --la cache de iconos nunca se escribia.
bool makeDirs(const std::string& path) {
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
    return mkdir(path.c_str(), 0777) == 0 || errno == EEXIST;
}

} // namespace

std::string homebrewIconCacheDir() { return kIconDir; }

bool loadHomebrewIconBytes(const HomebrewIconSource& source,
                           std::vector<uint8_t>& bytes, std::string& error) {
    const std::string cached = homebrewIconCacheDir() + "/" + cacheFileName(source.cacheKey);
    if (readWhole(cached, bytes)) {
        error.clear();
        return true;
    }

    std::string lastError = "La app no tiene icono.";
    if (!source.localNro.empty()) {
        std::string e;
        if (readNroIcon(localNroReader(source.localNro), bytes, e)) {
            lastError.clear();
        } else {
            lastError = e;
        }
    }
    if (bytes.empty() && !source.remoteNro.empty()) {
        std::string e;
        if (readNroIcon(remoteNroReader(source.remoteNro), bytes, e)) {
            lastError.clear();
        } else {
            lastError = e;
        }
    }
    if (bytes.empty() && !source.iconUrl.empty()) {
        std::string e;
        if (downloadBytes(source.iconUrl, 0, 0, kIconLimit, bytes, e)) {
            lastError.clear();
        } else {
            lastError = e;
        }
    }
    if (bytes.empty()) {
        error = lastError;
        return false;
    }
    // Un fallo al guardar no impide usar el icono: solo se vuelve a bajar.
    if (makeDirs(homebrewIconCacheDir()))
        writeWhole(cached, bytes);
    error.clear();
    return true;
}

bool decodeHomebrewIcon(const std::vector<uint8_t>& bytes,
                        std::vector<uint8_t>& rgba, int& width, int& height) {
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                            &width, &height, &channels, 4);
    if (!pixels)
        return false;
    rgba.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    shrinkRgbaToFit(rgba, width, height, kIconMaxSide);
    return true;
}

} // namespace gamehubnx
