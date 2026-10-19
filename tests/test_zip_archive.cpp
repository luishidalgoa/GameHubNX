// Lector de zip de los paquetes HB App Store: entradas, extraccion con CRC y
// rechazo de lo que no debe tocarse (rutas peligrosas, enlaces, corrupcion).

#include "app/zip_archive.hpp"

#include <sys/stat.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using gamehubnx::isSafeZipPath;
using gamehubnx::ZipArchive;
using gamehubnx::ZipEntry;

const char* OutDir = "build-pc/tests/zip-out";

// Mismo patron que genero el fixture sample.zip.
std::string expectedBlob() {
    std::string blob(200 * 1024, '\0');
    for (size_t i = 0; i < blob.size(); ++i)
        blob[i] = static_cast<char>((i * 31 + (i >> 7)) % 251);
    return blob;
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void testSafePaths() {
    assert(isSafeZipPath("switch/app/app.nro"));
    assert(isSafeZipPath("a..b/file"));          // ".." dentro de un nombre
    assert(!isSafeZipPath("../evil.txt"));
    assert(!isSafeZipPath("a/../b"));
    assert(!isSafeZipPath("/absoluta"));
    assert(!isSafeZipPath("a\\b"));
    assert(!isSafeZipPath(""));
    assert(!isSafeZipPath(std::string("bad\x01name")));
}

void testExtractsSampleArchive() {
    ZipArchive zip;
    std::string error;
    assert(zip.open("tests/fixtures/homebrew/sample.zip", error));
    assert(zip.entries().size() == 3);

    mkdir("build-pc/tests", 0777);
    const std::string out = OutDir;
    for (const ZipEntry& entry : zip.entries()) {
        assert(zip.extract(entry, out + "/" + entry.name, error));
    }
    assert(readFile(out + "/switch/app/app.nro") == expectedBlob());
    assert(readFile(out + "/info.json") == "{\"version\":\"1.0.0\"}\n");
    assert(readFile(out + "/switch/app/stored.txt").size() ==
           std::strlen("guardado sin comprimir\n") * 40);
}

void testRejectsTraversal() {
    ZipArchive zip;
    std::string error;
    assert(!zip.open("tests/fixtures/homebrew/traversal.zip", error));
    assert(error.find("peligrosa") != std::string::npos);
}

void testRejectsSymlinks() {
    ZipArchive zip;
    std::string error;
    assert(!zip.open("tests/fixtures/homebrew/symlink.zip", error));
    assert(error.find("enlaces") != std::string::npos);
}

void testCorruptEntryIsNotLeftBehind() {
    ZipArchive zip;
    std::string error;
    assert(zip.open("tests/fixtures/homebrew/corrupt.zip", error));
    const std::string target = std::string(OutDir) + "/switch/bad.txt";
    std::remove(target.c_str());
    assert(!zip.extract(zip.entries()[0], target, error));
    assert(error.find("corrupto") != std::string::npos);
    // Ni el fichero final ni el temporal quedan en la SD.
    struct stat st;
    assert(stat(target.c_str(), &st) != 0);
    assert(stat((target + ".part").c_str(), &st) != 0);
}

void testRejectsNonZip() {
    ZipArchive zip;
    std::string error;
    assert(!zip.open("tests/fixtures/homebrew/info.json.missing", error));
}

} // namespace

int main() {
    testSafePaths();
    testExtractsSampleArchive();
    testRejectsTraversal();
    testRejectsSymlinks();
    testCorruptEntryIsNotLeftBehind();
    testRejectsNonZip();
    std::printf("test_zip_archive: ok\n");
    return 0;
}
