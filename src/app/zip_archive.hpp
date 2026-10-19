#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gamehubnx {

// Una entrada del directorio central de un zip.
struct ZipEntry {
    std::string name;            // tal cual en el zip, con sus "/"
    uint16_t method = 0;         // 0 = guardado sin comprimir, 8 = deflate
    uint32_t crc32 = 0;
    uint64_t compressedSize = 0;
    uint64_t uncompressedSize = 0;
    uint64_t localOffset = 0;
    bool directory = false;
};

// Lector de zip de solo lectura, pensado para paquetes homebrew: ficheros
// pequenos o medianos, sin ZIP64, sin cifrar. Lee el zip por partes desde disco
// y nunca lo carga entero en memoria.
class ZipArchive {
public:
    // Lee el directorio central. Falla si no es un zip, si usa ZIP64, si
    // trae cifrado, o si alguna entrada es un enlace simbolico o una ruta
    // peligrosa: en ese caso el zip entero se rechaza, no se extrae a medias.
    bool open(const std::string& path, std::string& error);

    const std::vector<ZipEntry>& entries() const { return entries_; }

    // Extrae una entrada a destPath creando los directorios que falten. Verifica
    // el tamaño y el CRC32 contra lo que dice el directorio central.
    bool extract(const ZipEntry& entry, const std::string& destPath,
                 std::string& error) const;

private:
    std::string path_;
    std::vector<ZipEntry> entries_;
};

// Ruta relativa segura para escribir dentro de la SD: no vacia, sin "..", sin
// absolutas ni barras invertidas, sin bytes de control.
bool isSafeZipPath(const std::string& name);

} // namespace gamehubnx
