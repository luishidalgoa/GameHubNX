#pragma once

// Caché del comprimido de un paquete (NSZ) mientras se baja.
//
// Un NSZ sólido no se puede retomar desde la mitad: zstd no guarda el estado del
// descompresor, así que el flujo no da puntos de retorno y pausar tiraba todo lo
// bajado. Para no volver a bajarlo, los bytes comprimidos se guardan en orden en
// un fichero dentro de la carpeta de la tarea. Al reanudar se descomprimen desde
// ese fichero y la red solo baja lo que falta.
//
// El fichero es un prefijo exacto del paquete comprimido: nunca tiene huecos, así
// que su tamaño dice hasta dónde llega. Todo lo que no encaje (un tamaño mayor
// que el paquete, una escritura que falla, un espacio que no alcanza) se tira.

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <ctime>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace gamehubnx {
namespace input_cache {

/// Una caché sin cambios en este tiempo se considera abandonada y se borra.
constexpr time_t kStaleAfterSeconds = 14 * 24 * 60 * 60;
/// Espacio que debe quedar libre aparte de lo que falta por bajar.
constexpr uint64_t kSpareBytes = 256ull * 1024 * 1024;
constexpr size_t kReplayChunk = 1024 * 1024;

inline uint64_t hashName(const std::string& text) {
    uint64_t hash = 1469598103934665603ull;  // FNV-1a
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

/// Ruta de la caché de un paquete dentro de la carpeta de su tarea.
inline std::string pathFor(const std::string& dataPath, const std::string& packageId) {
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hashName(packageId)));
    return dataPath + "/input-" + hex + ".cache";
}

inline bool isCacheName(const std::string& name) {
    return name.size() > 12 && name.rfind("input-", 0) == 0 &&
           name.compare(name.size() - 6, 6, ".cache") == 0;
}

/// Bytes ya guardados (0 si no hay caché).
inline uint64_t cachedLength(const std::string& path) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
        return 0;
    return static_cast<uint64_t>(st.st_size);
}

/// ¿Hay sitio para guardar lo que falta por bajar, con un margen?
inline bool hasRoomFor(const std::string& dataPath, uint64_t missingBytes) {
    struct statvfs info {};
    if (statvfs(dataPath.c_str(), &info) != 0)
        return false;
    const uint64_t available = static_cast<uint64_t>(info.f_bavail) * info.f_frsize;
    return available >= missingBytes + kSpareBytes;
}

/// Borra las cachés de una carpeta de tarea. Devuelve cuántas borró.
inline size_t removeCachesIn(const std::string& dataPath) {
    DIR* dir = opendir(dataPath.c_str());
    if (!dir)
        return 0;
    size_t removed = 0;
    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (isCacheName(name) && unlink((dataPath + "/" + name).c_str()) == 0)
            ++removed;
    }
    closedir(dir);
    return removed;
}

/// Escritor en orden. Cualquier fallo desactiva la caché y borra lo guardado.
class Writer {
public:
    /// Abre la caché en `path` con `expected` bytes ya guardados. Si no coincide, se empieza de cero.
    Writer(std::string path, uint64_t expected) : path_(std::move(path)) {
        const uint64_t have = cachedLength(path_);
        if (expected == 0 || have != expected) {
            if (have != 0 || expected != 0)
                unlink(path_.c_str());
            file_ = std::fopen(path_.c_str(), "wb");
        } else {
            file_ = std::fopen(path_.c_str(), "ab");
        }
        if (file_)
            std::setvbuf(file_, nullptr, _IOFBF, 256 * 1024);
        else
            failed_ = true;
    }
    ~Writer() { close(); }
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    bool active() const { return file_ != nullptr && !failed_; }

    bool write(const uint8_t* data, size_t size) {
        if (!active())
            return false;
        if (std::fwrite(data, 1, size, file_) != size) {
            fail();
            return false;
        }
        return true;
    }

    /// Cierra el fichero y, si algo falló, lo borra.
    void close() {
        if (file_) {
            if (std::fclose(file_) != 0)
                failed_ = true;
            file_ = nullptr;
        }
        if (failed_)
            unlink(path_.c_str());
    }

    /// Descarta la caché: el paquete ya no se puede reanudar desde ella.
    void discard() {
        failed_ = true;
        close();
    }

private:
    void fail() {
        failed_ = true;
        close();
    }

    std::string path_;
    std::FILE* file_ = nullptr;
    bool failed_ = false;
};

/// Da a `sink` los primeros `length` bytes de la caché, en trozos. Falla si la caché es más corta.
inline bool replay(const std::string& path, uint64_t length,
                   const std::function<bool(const uint8_t*, size_t)>& sink,
                   const std::function<bool()>& stopped) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
        return false;
    std::vector<uint8_t> chunk(kReplayChunk);
    uint64_t left = length;
    bool ok = true;
    while (left > 0 && ok) {
        if (stopped()) {
            ok = false;
            break;
        }
        const size_t want = left < chunk.size() ? static_cast<size_t>(left) : chunk.size();
        const size_t got = std::fread(chunk.data(), 1, want, file);
        if (got == 0) {
            ok = false;
            break;
        }
        if (!sink(chunk.data(), got)) {
            ok = false;
            break;
        }
        left -= got;
    }
    std::fclose(file);
    return ok && left == 0;
}

/// Caché de una carpeta que ya no pertenece a ninguna tarea, o que lleva demasiado sin cambios.
inline bool isStale(const std::string& path, bool orphan, time_t now) {
    if (orphan)
        return true;
    struct stat st {};
    if (stat(path.c_str(), &st) != 0)
        return false;
    return now - st.st_mtime > kStaleAfterSeconds;
}

/// Limpieza al arrancar: borra las cachés de carpetas que ya no son de ninguna tarea y las que
/// llevan demasiado sin cambios. Devuelve cuántas borró.
inline size_t sweepOrphanCaches(const std::string& downloadRoot,
                                const std::set<std::string>& inUse, time_t now) {
    DIR* root = opendir(downloadRoot.c_str());
    if (!root)
        return 0;
    size_t removed = 0;
    while (dirent* entry = readdir(root)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;
        const std::string dataPath = downloadRoot + "/" + name;
        const bool orphan = inUse.count(dataPath) == 0;
        DIR* dir = opendir(dataPath.c_str());
        if (!dir)
            continue;
        while (dirent* file = readdir(dir)) {
            const std::string fileName = file->d_name;
            if (!isCacheName(fileName))
                continue;
            const std::string path = dataPath + "/" + fileName;
            if (isStale(path, orphan, now) && unlink(path.c_str()) == 0)
                ++removed;
        }
        closedir(dir);
    }
    closedir(root);
    return removed;
}

} // namespace input_cache
} // namespace gamehubnx
