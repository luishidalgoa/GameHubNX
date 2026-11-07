// Caché del comprimido de un NSZ: se guarda en orden, se reproduce al reanudar y se limpia.
#include "../src/app/input_cache.hpp"

#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace gamehubnx::input_cache;

static std::string makeTempDir() {
    char pattern[] = "/tmp/gh-input-cache-XXXXXX";
    char* dir = mkdtemp(pattern);
    assert(dir != nullptr);
    return dir;
}

static void writeFile(const std::string& path, const std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    assert(f);
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

static void ageFile(const std::string& path, time_t secondsAgo) {
    const time_t when = time(nullptr) - secondsAgo;
    struct timeval times[2] = {{when, 0}, {when, 0}};
    assert(utimes(path.c_str(), times) == 0);
}

int main() {
    // Nombre estable y reconocible.
    const std::string path = pathFor("/d/task", "Life is Strange.nsz");
    assert(path == pathFor("/d/task", "Life is Strange.nsz"));
    assert(path.rfind("/d/task/input-", 0) == 0);
    assert(isCacheName(path.substr(std::string("/d/task/").size())));
    assert(!isCacheName("queue.bencode"));

    const std::string dir = makeTempDir();
    const std::string cache = pathFor(dir, "pkg.nsz");

    // Se escribe en orden y el tamaño dice hasta dónde llega.
    {
        Writer w(cache, 0);
        assert(w.active());
        const std::string a = "0123456789";
        assert(w.write(reinterpret_cast<const uint8_t*>(a.data()), a.size()));
    }
    assert(cachedLength(cache) == 10);

    // Reanudar con el mismo tamaño añade al final.
    {
        Writer w(cache, 10);
        assert(w.active());
        const std::string b = "abcdef";
        assert(w.write(reinterpret_cast<const uint8_t*>(b.data()), b.size()));
    }
    assert(cachedLength(cache) == 16);

    // Con un tamaño que no cuadra, se empieza de cero.
    {
        Writer w(cache, 3);
        assert(w.active());
    }
    assert(cachedLength(cache) == 0);

    // Reproducir da exactamente lo guardado, en orden.
    writeFile(cache, "HELLO WORLD");
    std::string got;
    const bool ok = replay(cache, 11, [&got](const uint8_t* d, size_t n) {
        got.append(reinterpret_cast<const char*>(d), n);
        return true;
    }, [] { return false; });
    assert(ok && got == "HELLO WORLD");

    // Pedir más de lo que hay falla: nunca se inventa lo que falta.
    got.clear();
    assert(!replay(cache, 20, [&got](const uint8_t* d, size_t n) {
        got.append(reinterpret_cast<const char*>(d), n);
        return true;
    }, [] { return false; }));

    // Parar a mitad de la reproducción devuelve falso.
    assert(!replay(cache, 11, [](const uint8_t*, size_t) { return true; }, [] { return true; }));

    // Espacio: pedir muchísimo no cabe; pedir poco sí.
    assert(hasRoomFor(dir, 1024));
    assert(!hasRoomFor(dir, 1ull << 60));

    // Limpieza al arrancar: una caché huérfana y una abandonada se borran; una reciente de tarea viva se queda.
    const std::string liveDir = dir + "/live";
    const std::string orphanDir = dir + "/orphan";
    mkdir(liveDir.c_str(), 0755);
    mkdir(orphanDir.c_str(), 0755);
    const std::string liveCache = pathFor(liveDir, "a.nsz");
    const std::string orphanCache = pathFor(orphanDir, "b.nsz");
    const std::string staleCache = pathFor(liveDir, "c.nsz");
    writeFile(liveCache, "live");
    writeFile(orphanCache, "orphan");
    writeFile(staleCache, "stale");
    ageFile(staleCache, kStaleAfterSeconds + 60);
    const std::set<std::string> inUse = {liveDir};
    const size_t removed = sweepOrphanCaches(dir, inUse, time(nullptr));
    assert(removed == 2);
    assert(cachedLength(liveCache) == 4);
    assert(cachedLength(orphanCache) == 0);
    assert(cachedLength(staleCache) == 0);

    // Al quitar una tarea sin borrar datos, sus cachés desaparecen.
    writeFile(liveCache, "live");
    assert(removeCachesIn(liveDir) == 1);
    assert(cachedLength(liveCache) == 0);

    std::printf("input cache tests passed\n");
    return 0;
}
