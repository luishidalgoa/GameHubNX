#include "homebrew_icons.hpp"

#include "app/homebrew_source.hpp"
#include "app/nro_metadata.hpp"

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
    return std::rename(partial.c_str(), path.c_str()) == 0;
}

bool makeDirs(const std::string& path) {
    for (size_t slash = path.find('/', 1); slash != std::string::npos;
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
    const std::string cached = homebrewIconCacheDir() + "/" + source.cacheKey + ".img";
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
    return true;
}

} // namespace gamehubnx
