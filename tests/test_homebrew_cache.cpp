// Cache en disco de las fuentes de homebrew: que se reutiliza mientras esta
// fresca, que se refresca cuando caduca o se fuerza, y que sin red se sirve lo
// guardado avisando de que es viejo.

#include "app/homebrew_cache.hpp"

#include <cassert>
#include <cstdio>
#include <functional>
#include <string>

namespace {

using gamehubnx::HomebrewCacheEntry;
using gamehubnx::loadSourceBody;

const char* Root = "build-pc/tests/homebrew-cache-out";
constexpr int64_t Ttl = gamehubnx::kHomebrewCacheTtlSec;

// Descargador de mentira: cuenta cuantas veces se le pidio la red.
struct FakeNetwork {
    bool up = true;
    std::string body = "respuesta-nueva";
    int calls = 0;
    bool operator()(std::string& out, std::string& error) {
        ++calls;
        if (!up) {
            error = "Sin conexion.";
            return false;
        }
        out = body;
        return true;
    }
};

void reset(const std::string& key) {
    std::remove(gamehubnx::homebrewCacheFile(Root, key).c_str());
}

void testKeysAreFilenameSafe() {
    assert(gamehubnx::homebrewCacheKeyForGithub("NextendoNetwork/Prelude-Nextendo") ==
           "gh-NextendoNetwork_Prelude-Nextendo");
    assert(gamehubnx::homebrewCacheKeyForHb() == "hb-switch");
    assert(gamehubnx::homebrewCacheKeyForRepoList() == "gamehub-repos");
}

void testFreshnessRules() {
    HomebrewCacheEntry entry{1000, "x"};
    assert(gamehubnx::homebrewCacheIsFresh(entry, 1000 + Ttl - 1, Ttl));
    assert(!gamehubnx::homebrewCacheIsFresh(entry, 1000 + Ttl, Ttl));
    // Reloj no fiable (marca 0) o del futuro: nunca fresca.
    assert(!gamehubnx::homebrewCacheIsFresh({0, "x"}, 5, Ttl));
    assert(!gamehubnx::homebrewCacheIsFresh(entry, 500, Ttl));
}

void testFreshCacheSkipsNetwork() {
    const std::string key = "test-fresh";
    reset(key);
    const int64_t now = 100000;
    std::string error;
    assert(gamehubnx::writeHomebrewCache(
        gamehubnx::homebrewCacheFile(Root, key), {now - 60, "en-cache"}, error));

    FakeNetwork net;
    auto out = loadSourceBody(Root, key, false, now, Ttl, std::ref(net));
    assert(out.ok && !out.stale);
    assert(out.body == "en-cache");
    assert(out.ageSec == 60);
    assert(net.calls == 0);
}

void testMissingCacheDownloadsAndStores() {
    const std::string key = "test-missing";
    reset(key);
    const int64_t now = 200000;
    FakeNetwork net;
    auto out = loadSourceBody(Root, key, false, now, Ttl, std::ref(net));
    assert(out.ok && !out.stale);
    assert(out.body == "respuesta-nueva");
    assert(net.calls == 1);

    // Lo descargado queda guardado con su marca de tiempo.
    HomebrewCacheEntry stored;
    assert(gamehubnx::readHomebrewCache(gamehubnx::homebrewCacheFile(Root, key),
                                        stored));
    assert(stored.fetchedAt == now);
    assert(stored.body == "respuesta-nueva");
}

void testStaleCacheIsServedWhenOffline() {
    const std::string key = "test-stale";
    reset(key);
    const int64_t then = 300000;
    std::string error;
    assert(gamehubnx::writeHomebrewCache(
        gamehubnx::homebrewCacheFile(Root, key), {then, "viejo"}, error));

    FakeNetwork net;
    net.up = false;
    const int64_t now = then + 2 * Ttl;
    auto out = loadSourceBody(Root, key, false, now, Ttl, std::ref(net));
    assert(net.calls == 1);            // caduca: intento la red...
    assert(out.ok && out.stale);       // ...y sin red sirve lo guardado
    assert(out.body == "viejo");
    assert(out.ageSec == 2 * Ttl);
}

void testNoCacheNoNetworkReportsError() {
    const std::string key = "test-nothing";
    reset(key);
    FakeNetwork net;
    net.up = false;
    auto out = loadSourceBody(Root, key, false, 400000, Ttl, std::ref(net));
    assert(!out.ok);
    assert(out.error == "Sin conexion.");
}

void testForceRefreshIgnoresFreshCache() {
    const std::string key = "test-force";
    reset(key);
    const int64_t now = 500000;
    std::string error;
    assert(gamehubnx::writeHomebrewCache(
        gamehubnx::homebrewCacheFile(Root, key), {now - 10, "reciente"}, error));

    FakeNetwork net;
    auto out = loadSourceBody(Root, key, true, now, Ttl, std::ref(net));
    assert(net.calls == 1);
    assert(out.ok && !out.stale);
    assert(out.body == "respuesta-nueva");
}

} // namespace

int main() {
    testKeysAreFilenameSafe();
    testFreshnessRules();
    testFreshCacheSkipsNetwork();
    testMissingCacheDownloadsAndStores();
    testStaleCacheIsServedWhenOffline();
    testNoCacheNoNetworkReportsError();
    testForceRefreshIgnoresFreshCache();
    std::printf("test_homebrew_cache: ok\n");
    return 0;
}
