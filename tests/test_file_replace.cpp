// replaceFile: el reemplazo que no pierde el fichero viejo si algo falla. En PC
// rename() ya sobrescribe; el camino de "apartar y volver" se prueba igual.

#include "app/file_replace.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace {

const std::string Dir = "/tmp/gamehubnx-file-replace";
const std::string Target = Dir + "/target.bin";
const std::string Source = Dir + "/source.part";

void write(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

bool exists(const std::string& path) {
    std::ifstream in(path);
    return in.good();
}

void testReplacesExistingFile() {
    write(Target, "viejo");
    write(Source, "nuevo");
    assert(gamehubnx::replaceFile(Source, Target));
    assert(read(Target) == "nuevo");
    assert(!exists(Source));
    assert(!exists(Target + ".old"));
}

void testCreatesWhenTargetMissing() {
    std::remove(Target.c_str());
    write(Source, "primero");
    assert(gamehubnx::replaceFile(Source, Target));
    assert(read(Target) == "primero");
    assert(!exists(Source));
}

void testMissingSourceKeepsOldFile() {
    write(Target, "intacto");
    std::remove(Source.c_str());
    assert(!gamehubnx::replaceFile(Source, Target));
    assert(read(Target) == "intacto");
    assert(!exists(Target + ".old"));
}

} // namespace

int main() {
    const std::string command = "mkdir -p " + Dir;
    (void)std::system(command.c_str());
    testReplacesExistingFile();
    testCreatesWhenTargetMissing();
    testMissingSourceKeepsOldFile();
    std::remove(Target.c_str());
    std::remove(Source.c_str());
    std::printf("test_file_replace: ok\n");
    return 0;
}
