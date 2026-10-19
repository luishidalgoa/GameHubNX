#include "zip_archive.hpp"

#include <zlib.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>

#include <sys/stat.h>

namespace gamehubnx {
namespace {

constexpr uint32_t kEocdSig = 0x06054b50;
constexpr uint32_t kCentralSig = 0x02014b50;
constexpr uint32_t kLocalSig = 0x04034b50;
// El EOCD son 22 bytes mas un comentario de hasta 65535.
constexpr size_t kEocdSearch = 22 + 65535;
constexpr size_t kChunk = 32 * 1024;

uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool readAt(std::ifstream& in, uint64_t offset, uint8_t* buf, size_t len) {
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(len));
    return static_cast<size_t>(in.gcount()) == len;
}

// Crea los directorios intermedios de una ruta de fichero.
bool makeParentDirectories(const std::string& file) {
    for (size_t slash = file.find('/', 1); slash != std::string::npos;
         slash = file.find('/', slash + 1)) {
        const std::string part = file.substr(0, slash);
        if (mkdir(part.c_str(), 0777) != 0 && errno != EEXIST)
            return false;
    }
    return true;
}

} // namespace

bool isSafeZipPath(const std::string& name) {
    if (name.empty() || name[0] == '/' || name.find('\\') != std::string::npos)
        return false;
    // Cada segmento se mira por separado: "a..b" es un nombre normal,
    // "a/../b" no.
    size_t start = 0;
    while (start <= name.size()) {
        size_t end = name.find('/', start);
        if (end == std::string::npos)
            end = name.size();
        const std::string part = name.substr(start, end - start);
        if (part == "..")
            return false;
        for (unsigned char c : part)
            if (c < 0x20 || c == 0x7f)
                return false;
        start = end + 1;
    }
    return true;
}

bool ZipArchive::open(const std::string& path, std::string& error) {
    entries_.clear();
    path_ = path;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "No se pudo abrir " + path + ".";
        return false;
    }
    in.seekg(0, std::ios::end);
    const uint64_t size = static_cast<uint64_t>(in.tellg());
    if (size < 22) {
        error = "El paquete es demasiado pequeño para ser un zip.";
        return false;
    }

    // El EOCD esta al final del fichero, delante de un comentario opcional.
    const size_t window = size < kEocdSearch ? static_cast<size_t>(size)
                                             : kEocdSearch;
    std::vector<uint8_t> tail(window);
    if (!readAt(in, size - window, tail.data(), window)) {
        error = "No se pudo leer el final del zip.";
        return false;
    }
    size_t eocd = window;
    for (size_t i = window - 22 + 1; i-- > 0;) {
        if (le32(&tail[i]) == kEocdSig) {
            eocd = i;
            break;
        }
    }
    if (eocd == window) {
        error = "No es un zip valido.";
        return false;
    }
    const uint8_t* end = &tail[eocd];
    const uint16_t count = le16(end + 10);
    const uint32_t centralSize = le32(end + 12);
    const uint32_t centralOffset = le32(end + 16);
    if (count == 0xFFFF || centralSize == 0xFFFFFFFFu ||
        centralOffset == 0xFFFFFFFFu) {
        error = "El zip usa ZIP64, que no soportamos.";
        return false;
    }

    std::vector<uint8_t> central(centralSize);
    if (centralSize && !readAt(in, centralOffset, central.data(), centralSize)) {
        error = "El directorio del zip esta truncado.";
        return false;
    }

    size_t at = 0;
    for (uint16_t i = 0; i < count; ++i) {
        if (at + 46 > central.size() || le32(&central[at]) != kCentralSig) {
            error = "El directorio central del zip esta corrupto.";
            return false;
        }
        const uint8_t* h = &central[at];
        const uint16_t flags = le16(h + 8);
        const uint16_t nameLen = le16(h + 28);
        const uint16_t extraLen = le16(h + 30);
        const uint16_t commentLen = le16(h + 32);
        const uint32_t externalAttrs = le32(h + 38);
        if (at + 46 + nameLen + extraLen + commentLen > central.size()) {
            error = "El directorio central del zip esta corrupto.";
            return false;
        }
        if (flags & 0x0001) {
            error = "El zip esta cifrado.";
            return false;
        }
        // En los bits altos de los atributos externos va el modo unix; un
        // enlace simbolico no se extrae nunca.
        const uint32_t mode = externalAttrs >> 16;
        if ((mode & 0170000) == 0120000) {
            error = "El zip contiene enlaces simbolicos.";
            return false;
        }

        ZipEntry entry;
        entry.name.assign(reinterpret_cast<const char*>(h + 46), nameLen);
        entry.method = le16(h + 10);
        entry.crc32 = le32(h + 16);
        entry.compressedSize = le32(h + 20);
        entry.uncompressedSize = le32(h + 24);
        entry.localOffset = le32(h + 42);
        entry.directory = !entry.name.empty() && entry.name.back() == '/';
        if (entry.uncompressedSize == 0xFFFFFFFFu ||
            entry.compressedSize == 0xFFFFFFFFu ||
            entry.localOffset == 0xFFFFFFFFu) {
            error = "El zip usa ZIP64, que no soportamos.";
            return false;
        }
        if (!isSafeZipPath(entry.name)) {
            error = "El zip contiene una ruta peligrosa: " + entry.name;
            return false;
        }
        if (entry.method != 0 && entry.method != 8) {
            error = "Metodo de compresion no soportado en " + entry.name + ".";
            return false;
        }
        entries_.push_back(std::move(entry));
        at += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

bool ZipArchive::extract(const ZipEntry& entry, const std::string& destPath,
                         std::string& error) const {
    if (entry.directory) {
        if (!makeParentDirectories(destPath + "/") ||
            (mkdir(destPath.c_str(), 0777) != 0 && errno != EEXIST)) {
            error = "No se pudo crear " + destPath + ".";
            return false;
        }
        return true;
    }

    std::ifstream in(path_, std::ios::binary);
    if (!in) {
        error = "No se pudo reabrir el zip.";
        return false;
    }
    uint8_t localHeader[30];
    if (!readAt(in, entry.localOffset, localHeader, sizeof localHeader) ||
        le32(localHeader) != kLocalSig) {
        error = "Cabecera local corrupta en " + entry.name + ".";
        return false;
    }
    const uint64_t dataOffset = entry.localOffset + 30 +
                                le16(localHeader + 26) + le16(localHeader + 28);

    if (!makeParentDirectories(destPath)) {
        error = "No se pudo crear la carpeta de " + destPath + ".";
        return false;
    }
    // Se escribe a un temporal y se renombra al final: un corte no deja un
    // fichero a medias donde la consola lo busque.
    const std::string partial = destPath + ".part";
    std::ofstream out(partial, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "No se pudo escribir " + destPath + ".";
        return false;
    }

    uint32_t crc = crc32(0L, Z_NULL, 0);
    uint64_t written = 0;
    bool ok = true;
    std::vector<uint8_t> input(kChunk);
    std::vector<uint8_t> output(kChunk);

    if (entry.method == 0) {
        uint64_t remaining = entry.compressedSize;
        uint64_t at = dataOffset;
        while (remaining > 0 && ok) {
            const size_t take = remaining < kChunk ? static_cast<size_t>(remaining)
                                                   : kChunk;
            if (!readAt(in, at, input.data(), take)) {
                ok = false;
                break;
            }
            out.write(reinterpret_cast<const char*>(input.data()),
                      static_cast<std::streamsize>(take));
            crc = crc32(crc, input.data(), static_cast<uInt>(take));
            written += take;
            at += take;
            remaining -= take;
        }
    } else {
        z_stream strm;
        std::memset(&strm, 0, sizeof strm);
        // -MAX_WBITS: deflate crudo, sin cabecera zlib, como lo escribe zip.
        if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
            error = "No se pudo iniciar la descompresion.";
            out.close();
            std::remove(partial.c_str());
            return false;
        }
        uint64_t remaining = entry.compressedSize;
        uint64_t at = dataOffset;
        int status = Z_OK;
        while (status != Z_STREAM_END && ok) {
            if (strm.avail_in == 0) {
                if (remaining == 0) {
                    ok = false;  // el deflate termino antes de tiempo
                    break;
                }
                const size_t take = remaining < kChunk
                                        ? static_cast<size_t>(remaining)
                                        : kChunk;
                if (!readAt(in, at, input.data(), take)) {
                    ok = false;
                    break;
                }
                strm.next_in = input.data();
                strm.avail_in = static_cast<uInt>(take);
                at += take;
                remaining -= take;
            }
            strm.next_out = output.data();
            strm.avail_out = static_cast<uInt>(output.size());
            status = inflate(&strm, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END) {
                ok = false;
                break;
            }
            const size_t produced = output.size() - strm.avail_out;
            out.write(reinterpret_cast<const char*>(output.data()),
                      static_cast<std::streamsize>(produced));
            crc = crc32(crc, output.data(), static_cast<uInt>(produced));
            written += produced;
        }
        inflateEnd(&strm);
    }
    out.close();

    if (!ok || !out || written != entry.uncompressedSize || crc != entry.crc32) {
        std::remove(partial.c_str());
        error = "El fichero " + entry.name + " del zip esta corrupto.";
        return false;
    }
    if (std::rename(partial.c_str(), destPath.c_str()) != 0) {
        std::remove(partial.c_str());
        error = "No se pudo dejar " + destPath + " en su sitio.";
        return false;
    }
    error.clear();
    return true;
}

} // namespace gamehubnx
