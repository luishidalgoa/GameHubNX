// Escaneo de sdmc:/switch: .nro sueltos, carpetas instaladas por nosotros (manda
// el fichero de version), .nro puestos a mano (version del NACP), carpetas
// ocultas y .nro rotos.

#include "app/homebrew_installed.hpp"

#include <sys/stat.h>

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

using gamehubnx::findInstalledNro;
using gamehubnx::InstalledNro;
using gamehubnx::scanInstalledNros;

const char* Root = "build-pc/tests/switch-scan";

void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}
void put64(std::vector<uint8_t>& b, size_t at, uint64_t v) {
    for (int i = 0; i < 8; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}
void putText(std::vector<uint8_t>& b, size_t at, const std::string& s) {
    std::memcpy(b.data() + at, s.data(), s.size());
}

// .nro minimo con NACP: nombre, autor y version visible.
std::vector<uint8_t> makeNro(const std::string& name, const std::string& version) {
    const uint64_t asset = 0x100, nacpRel = 0x100, nacpSize = 0x4000;
    std::vector<uint8_t> b(asset + nacpRel + nacpSize, 0);
    putText(b, 0x10, "NRO0");
    put32(b, 0x18, static_cast<uint32_t>(asset));
    putText(b, asset, "ASET");
    put64(b, asset + 24, nacpRel);
    put64(b, asset + 32, nacpSize);
    putText(b, asset + nacpRel, name);
    putText(b, asset + nacpRel + 0x3060, version);
    return b;
}

void writeBytes(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}

void writeText(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

void setupTree() {
    std::system("rm -rf build-pc/tests/switch-scan");
    mkdir("build-pc/tests", 0777);
    mkdir(Root, 0777);
    const std::string root = Root;
    writeBytes(root + "/loose.nro", makeNro("Loose App", "1.0"));
    mkdir((root + "/tico").c_str(), 0777);
    writeBytes(root + "/tico/tico.nro", makeNro("Tico", "0.7.8"));
    writeText(root + "/tico/.gamehubnx-version", "v0.7.9\n");  // lo instalo la app
    mkdir((root + "/manual").c_str(), 0777);
    writeBytes(root + "/manual/app.nro", makeNro("Manual App", "2.0"));
    mkdir((root + "/.hidden").c_str(), 0777);
    writeBytes(root + "/.hidden/x.nro", makeNro("Hidden", "9"));
    mkdir((root + "/broken").c_str(), 0777);
    writeText(root + "/broken/broken.nro", "no es un nro");
    writeText(root + "/readme.txt", "ignorame");
}

const InstalledNro* byFile(const std::vector<InstalledNro>& list, const std::string& file) {
    for (const InstalledNro& nro : list)
        if (nro.fileName == file) return &nro;
    return nullptr;
}

void testListsEverythingUnderSwitch() {
    setupTree();
    const auto list = scanInstalledNros(Root);
    assert(list.size() == 4);  // loose, tico, manual, broken (no .hidden, no .txt)
    assert(byFile(list, "x.nro") == nullptr);
    assert(byFile(list, "readme.txt") == nullptr);

    const InstalledNro* loose = byFile(list, "loose.nro");
    assert(loose && loose->dirName.empty());
    assert(loose->name == "Loose App" && loose->version == "1.0");
    assert(!loose->versionFromInstaller);
}

void testInstallerVersionWinsInOwnFolders() {
    const auto list = scanInstalledNros(Root);
    const InstalledNro* tico = byFile(list, "tico.nro");
    assert(tico && tico->dirName == "tico");
    assert(tico->name == "Tico");
    assert(tico->version == "v0.7.9");   // el tag que dejo la app, no el NACP
    assert(tico->versionFromInstaller);
}

void testManualAppUsesNacpVersion() {
    const auto list = scanInstalledNros(Root);
    const InstalledNro* manual = byFile(list, "app.nro");
    assert(manual && manual->dirName == "manual");
    assert(manual->name == "Manual App");
    assert(manual->version == "2.0");
    assert(!manual->versionFromInstaller);
}

void testBrokenNroIsListedWithFileName() {
    const auto list = scanInstalledNros(Root);
    const InstalledNro* broken = byFile(list, "broken.nro");
    assert(broken);
    assert(broken->name == "broken");
    assert(broken->version.empty());
}

void testFindMatchesFolderThenName() {
    const auto list = scanInstalledNros(Root);
    const InstalledNro* exact = findInstalledNro(list, "TICO", "Tico.NRO");
    assert(exact && exact->dirName == "tico");
    // Puesto en otra carpeta: el nombre basta.
    const InstalledNro* moved = findInstalledNro(list, "otra", "app.nro");
    assert(moved && moved->dirName == "manual");
    // En la raiz.
    const InstalledNro* root = findInstalledNro(list, "", "loose.nro");
    assert(root && root->dirName.empty());
    assert(findInstalledNro(list, "tico", "no-existe.nro") == nullptr);
}

} // namespace

int main() {
    testListsEverythingUnderSwitch();
    testInstallerVersionWinsInOwnFolders();
    testManualAppUsesNacpVersion();
    testBrokenNroIsListedWithFileName();
    testFindMatchesFolderThenName();
    std::printf("test_homebrew_installed: ok\n");
    return 0;
}
