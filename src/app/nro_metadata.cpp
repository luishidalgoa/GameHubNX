#include "nro_metadata.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace gamehubnx {
namespace {

constexpr size_t kHeaderSize = 0x40;
constexpr size_t kNroMagicOffset = 0x10;
constexpr size_t kNroSizeOffset = 0x18;
constexpr size_t kAssetHeaderSize = 0x38;
// Tamanos maximos que aceptamos: el icono de homebrew ronda los 10 KB y el NACP
// es de 16 KB. Algo mucho mayor no es una cabecera valida.
constexpr uint64_t kMaxIconBytes = 512 * 1024;
constexpr uint64_t kMaxNacpBytes = 16 * 1024;
// Posiciones dentro del NACP (ver libnx, NacpStruct).
constexpr size_t kLanguageName = 0x000;   // 0x200 bytes, el primer idioma
constexpr size_t kLanguageAuthor = 0x200; // 0x100 bytes, mismo idioma
constexpr size_t kDisplayVersion = 0x3060; // 0x10 bytes
constexpr size_t kDisplayVersionSize = 0x10;

uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t le64(const uint8_t* p) {
    return static_cast<uint64_t>(le32(p)) |
           (static_cast<uint64_t>(le32(p + 4)) << 32);
}

// Texto terminado en cero dentro de un campo de longitud fija.
std::string fixedString(const std::vector<uint8_t>& data, size_t offset,
                        size_t length) {
    if (offset >= data.size())
        return {};
    const size_t end = std::min(data.size(), offset + length);
    size_t stop = offset;
    while (stop < end && data[stop] != 0)
        ++stop;
    return std::string(reinterpret_cast<const char*>(data.data() + offset),
                       stop - offset);
}

} // namespace

bool readNroAssetLayout(const NroReader& read, NroAssetLayout& out,
                        std::string& error) {
    std::vector<uint8_t> header;
    if (!read(0, kHeaderSize, header) || header.size() < kHeaderSize) {
        error = "No se pudo leer la cabecera.";
        return false;
    }
    if (std::memcmp(header.data() + kNroMagicOffset, "NRO0", 4) != 0) {
        error = "No es un .nro.";
        return false;
    }
    const uint64_t assetOffset = le32(header.data() + kNroSizeOffset);

    std::vector<uint8_t> asset;
    if (!read(assetOffset, kAssetHeaderSize, asset) ||
        asset.size() < kAssetHeaderSize ||
        std::memcmp(asset.data(), "ASET", 4) != 0) {
        error = "El .nro no trae cabecera de assets.";
        return false;
    }
    out.assetOffset = assetOffset;
    out.iconOffset = le64(asset.data() + 8);
    out.iconSize = le64(asset.data() + 16);
    out.nacpOffset = le64(asset.data() + 24);
    out.nacpSize = le64(asset.data() + 32);
    if (out.iconSize > kMaxIconBytes || out.nacpSize > kMaxNacpBytes) {
        error = "Tamano de assets fuera de rango.";
        return false;
    }
    error.clear();
    return true;
}

bool readNroInfo(const NroReader& read, NroInfo& out, std::string& error) {
    NroAssetLayout layout;
    if (!readNroAssetLayout(read, layout, error))
        return false;
    if (layout.nacpSize < kDisplayVersion + kDisplayVersionSize) {
        error = "NACP demasiado corto.";
        return false;
    }
    std::vector<uint8_t> nacp;
    if (!read(layout.assetOffset + layout.nacpOffset,
              static_cast<size_t>(layout.nacpSize), nacp) ||
        nacp.size() < kDisplayVersion + kDisplayVersionSize) {
        error = "No se pudo leer el NACP.";
        return false;
    }
    out.name = fixedString(nacp, kLanguageName, 0x200);
    out.author = fixedString(nacp, kLanguageAuthor, 0x100);
    out.displayVersion = fixedString(nacp, kDisplayVersion, kDisplayVersionSize);
    error.clear();
    return true;
}

bool readNroIcon(const NroReader& read, std::vector<uint8_t>& jpeg,
                 std::string& error) {
    NroAssetLayout layout;
    if (!readNroAssetLayout(read, layout, error))
        return false;
    if (layout.iconSize == 0) {
        error = "El .nro no trae icono.";
        return false;
    }
    if (!read(layout.assetOffset + layout.iconOffset,
              static_cast<size_t>(layout.iconSize), jpeg) ||
        jpeg.size() != layout.iconSize) {
        error = "No se pudo leer el icono.";
        return false;
    }
    // JPEG: FF D8 FF. Cualquier otra cosa no lo decodificamos.
    if (jpeg.size() < 3 || jpeg[0] != 0xFF || jpeg[1] != 0xD8 || jpeg[2] != 0xFF) {
        error = "El icono no es JPEG.";
        return false;
    }
    error.clear();
    return true;
}

NroReader localNroReader(const std::string& path) {
    return [path](uint64_t offset, size_t length, std::vector<uint8_t>& out) {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        in.seekg(static_cast<std::streamoff>(offset));
        out.resize(length);
        in.read(reinterpret_cast<char*>(out.data()),
                static_cast<std::streamsize>(length));
        return static_cast<size_t>(in.gcount()) == length;
    };
}

} // namespace gamehubnx
