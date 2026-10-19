// Cabecera NRO, bloque ASET, NACP e icono. Se prueba con un .nro sintetico que
// sigue la misma disposicion que los .nro reales.

#include "app/nro_metadata.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using gamehubnx::NroAssetLayout;
using gamehubnx::NroInfo;
using gamehubnx::NroReader;

constexpr uint64_t kAsset = 0x100;
constexpr uint64_t kIconRel = 0x80;
constexpr uint64_t kNacpRel = 0x100;
constexpr uint64_t kNacpSize = 0x4000;
const uint8_t kJpeg[8] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46};

void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}
void put64(std::vector<uint8_t>& b, size_t at, uint64_t v) {
    for (int i = 0; i < 8; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}
void putText(std::vector<uint8_t>& b, size_t at, const std::string& s) {
    std::memcpy(b.data() + at, s.data(), s.size());
}

// Un .nro minimo: cabecera NRO0, ASET en kAsset, icono JPEG y NACP.
std::vector<uint8_t> makeNro(bool withAsset = true, bool jpegIcon = true) {
    std::vector<uint8_t> b(kAsset + kNacpRel + kNacpSize, 0);
    putText(b, 0x10, "NRO0");
    put32(b, 0x18, static_cast<uint32_t>(kAsset));
    if (!withAsset) return b;
    putText(b, kAsset, "ASET");
    put32(b, kAsset + 4, 0);
    put64(b, kAsset + 8, kIconRel);
    put64(b, kAsset + 16, sizeof kJpeg);
    put64(b, kAsset + 24, kNacpRel);
    put64(b, kAsset + 32, kNacpSize);
    if (jpegIcon)
        std::memcpy(b.data() + kAsset + kIconRel, kJpeg, sizeof kJpeg);
    else
        b[kAsset + kIconRel] = 'P';  // no es JPEG
    putText(b, kAsset + kNacpRel, "Demo App");
    putText(b, kAsset + kNacpRel + 0x200, "Autor Demo");
    putText(b, kAsset + kNacpRel + 0x3060, "2.3.1");
    return b;
}

NroReader readerOver(const std::vector<uint8_t>& data) {
    return [&data](uint64_t offset, size_t length, std::vector<uint8_t>& out) {
        if (offset + length > data.size()) return false;
        out.assign(data.begin() + offset, data.begin() + offset + length);
        return true;
    };
}

void testReadsNameAuthorAndVersion() {
    const auto nro = makeNro();
    NroInfo info;
    std::string error;
    assert(gamehubnx::readNroInfo(readerOver(nro), info, error));
    assert(info.name == "Demo App");
    assert(info.author == "Autor Demo");
    assert(info.displayVersion == "2.3.1");
}

void testReadsJpegIcon() {
    const auto nro = makeNro();
    std::vector<uint8_t> icon;
    std::string error;
    assert(gamehubnx::readNroIcon(readerOver(nro), icon, error));
    assert(icon.size() == sizeof kJpeg);
    assert(std::memcmp(icon.data(), kJpeg, sizeof kJpeg) == 0);
}

void testRejectsNonNroAndMissingAssets() {
    NroAssetLayout layout;
    std::string error;
    std::vector<uint8_t> junk(0x200, 0x41);
    assert(!gamehubnx::readNroAssetLayout(readerOver(junk), layout, error));
    assert(error == "No es un .nro.");

    const auto noAssets = makeNro(false);
    assert(!gamehubnx::readNroAssetLayout(readerOver(noAssets), layout, error));
    assert(error == "El .nro no trae cabecera de assets.");
}

void testRejectsNonJpegIcon() {
    const auto nro = makeNro(true, false);
    std::vector<uint8_t> icon;
    std::string error;
    assert(!gamehubnx::readNroIcon(readerOver(nro), icon, error));
    assert(error == "El icono no es JPEG.");
}

void testRejectsOversizedNacp() {
    auto nro = makeNro();
    put64(nro, kAsset + 32, 64 * 1024 * 1024);  // NACP absurdo
    NroInfo info;
    std::string error;
    assert(!gamehubnx::readNroInfo(readerOver(nro), info, error));
}

} // namespace

int main() {
    testReadsNameAuthorAndVersion();
    testReadsJpegIcon();
    testRejectsNonNroAndMissingAssets();
    testRejectsNonJpegIcon();
    testRejectsOversizedNacp();
    std::printf("test_nro_metadata: ok\n");
    return 0;
}
