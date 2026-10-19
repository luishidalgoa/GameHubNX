#pragma once

#include <cstdint>
#include <string>

namespace gamehubnx {

// Respuesta guardada de una fuente de homebrew: su cuerpo tal cual llego y el
// momento en que se bajo.
struct HomebrewCacheEntry {
    int64_t fetchedAt = 0;  // segundos desde epoch; 0 si el reloj no era fiable
    std::string body;       // releases de GitHub o repo.json, sin interpretar
};

// GitHub limita las peticiones sin autenticar a 60 por hora y por IP. Con seis
// horas de margen, un dia de uso normal son unas decenas de peticiones.
constexpr int64_t kHomebrewCacheTtlSec = 6 * 60 * 60;

// Releases de cada repo de GitHub. Una release publicada tiene que verse en la
// app en media hora, no en seis horas: con seis, la app seguia ofreciendo la
// version anterior tras publicar una nueva. Con tres repos son como mucho seis
// peticiones por hora, dentro del limite de GitHub.
constexpr int64_t kGithubReleasesCacheTtlSec = 30 * 60;

// Carpeta de la cache en la SD.
std::string homebrewCacheRoot();

// Fichero de una fuente dentro de la cache: <root>/<key>.json.
std::string homebrewCacheFile(const std::string& root, const std::string& key);

// Claves estables por fuente. El repo de GitHub se aplana para que sea un nombre
// de fichero valido.
std::string homebrewCacheKeyForGithub(const std::string& repo);
std::string homebrewCacheKeyForHb();
std::string homebrewCacheKeyForRepoList();

// Formato: primera linea la marca de tiempo, el resto el cuerpo tal cual.
bool readHomebrewCache(const std::string& path, HomebrewCacheEntry& out);
bool writeHomebrewCache(const std::string& path, const HomebrewCacheEntry& entry,
                        std::string& error);

// Fresca si es de hace menos de `ttl`. Una marca 0 (reloj no fiable) o una del
// futuro nunca cuenta como fresca: en esos casos se vuelve a pedir a la red.
bool homebrewCacheIsFresh(const HomebrewCacheEntry& entry, int64_t now,
                          int64_t ttl);

// Lo que la vista necesita de una fuente: su cuerpo y de donde salio.
struct SourceBody {
    bool ok = false;
    std::string body;
    // Servido de una cache caducada porque la red fallo: la vista lo avisa.
    bool stale = false;
    // Antiguedad de la cache usada, en segundos. -1 si no se puede saber.
    int64_t ageSec = -1;
    std::string error;  // solo si !ok
};

// Cuerpo de una fuente, en este orden:
//   1. la cache, si esta fresca y no se fuerza la recarga;
//   2. la red, que ademas guarda la respuesta en la cache;
//   3. si la red falla, la cache aunque este caducada (marcada como stale);
//   4. si no hay nada de nada, el error de la red.
template <typename Download>
SourceBody loadSourceBody(const std::string& root, const std::string& key,
                          bool force, int64_t now, int64_t ttl,
                          Download&& download) {
    SourceBody out;
    const std::string path = homebrewCacheFile(root, key);
    HomebrewCacheEntry cached;
    const bool have = readHomebrewCache(path, cached);
    if (have && !force && homebrewCacheIsFresh(cached, now, ttl)) {
        out.ok = true;
        out.body = std::move(cached.body);
        out.ageSec = now - cached.fetchedAt;
        return out;
    }

    std::string body;
    std::string error;
    if (download(body, error)) {
        // Best effort: si el disco esta lleno, la respuesta buena se usa igual.
        std::string writeError;
        writeHomebrewCache(path, HomebrewCacheEntry{now, body}, writeError);
        out.ok = true;
        out.body = std::move(body);
        out.ageSec = 0;
        return out;
    }

    if (have) {
        out.ok = true;
        out.stale = true;
        out.body = std::move(cached.body);
        out.ageSec = cached.fetchedAt > 0 ? now - cached.fetchedAt : -1;
        return out;
    }
    out.error = error;
    return out;
}

} // namespace gamehubnx
