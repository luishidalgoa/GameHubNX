#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#include "app/homebrew_cache.hpp"
#include "app/homebrew_source.hpp"

namespace gamehubnx {

// Lo que sale de cargar todas las fuentes de homebrew.
struct HomebrewSourceLoad {
    std::vector<HomebrewApp> apps;
    std::string error;         // primer fallo que no tiene cache que lo cubra
    int64_t staleAgeSec = -1;  // >= 0 si algo se sirvio de cache caducada
};

inline void noteHomebrewStale(HomebrewSourceLoad& out, const SourceBody& src) {
    if (!src.stale)
        return;
    const int64_t age = src.ageSec < 0 ? 0 : src.ageSec;
    if (out.staleAgeSec < 0 || age > out.staleAgeSec)
        out.staleAgeSec = age;
}

inline std::string homebrewLowerAscii(std::string text) {
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Carga los repos de GitHub y el catalogo de HB App Store. Cada fuente decide
// por su cuenta si usa la cache (fresca), la red o la cache caducada (sin red).
// Un fallo en una fuente no tumba a las demas.
inline HomebrewSourceLoad loadHomebrewSources(bool force, int64_t now) {
    HomebrewSourceLoad out;
    const std::string root = homebrewCacheRoot();

    // Repos de GitHub: la lista que publica GameHub, con su cache. Si no hay ni
    // red ni cache valida, la lista fija de la app.
    std::vector<std::string> repos;
    {
        const SourceBody list = loadSourceBody(
            root, homebrewCacheKeyForRepoList(), force, now,
            kHomebrewCacheTtlSec,
            [](std::string& body, std::string& error) {
                return downloadHomebrewRepoList(body, error);
            });
        std::string listError;
        if (list.ok && parseHomebrewRepoList(list.body, repos, listError)) {
            noteHomebrewStale(out, list);
        } else {
            repos = defaultHomebrewRepos();
        }
    }

    for (const std::string& repo : repos) {
        const SourceBody src = loadSourceBody(
            root, homebrewCacheKeyForGithub(repo), force, now,
            kGithubReleasesCacheTtlSec,
            [&repo](std::string& body, std::string& error) {
                return downloadGithubReleases(repo, body, error);
            });
        if (!src.ok) {
            if (out.error.empty())
                out.error = src.error;
            continue;
        }
        noteHomebrewStale(out, src);
        HomebrewApp app;
        app.repo = repo;
        app.pageUrl = "https://github.com/" + repo;
        std::string parseError;
        if (parseGithubReleases(src.body, app.releases, parseError))
            out.apps.push_back(std::move(app));
        else if (out.error.empty())
            out.error = parseError;
    }

    const std::string base = homebrewSwitchRepoUrl();
    const SourceBody hb = loadSourceBody(
        root, homebrewCacheKeyForHb(), force, now, kHomebrewCacheTtlSec,
        [&base](std::string& body, std::string& error) {
            return downloadHbRepo(base, body, error);
        });
    if (!hb.ok) {
        if (out.error.empty())
            out.error = hb.error;
        return out;
    }
    noteHomebrewStale(out, hb);
    std::vector<HomebrewApp> hbApps;
    std::string parseError;
    if (!parseHbRepo(hb.body, base, hbApps, parseError)) {
        if (out.error.empty())
            out.error = parseError;
        return out;
    }
    std::sort(hbApps.begin(), hbApps.end(),
              [](const HomebrewApp& a, const HomebrewApp& b) {
                  return homebrewLowerAscii(a.displayName()) <
                         homebrewLowerAscii(b.displayName());
              });
    for (HomebrewApp& app : hbApps)
        out.apps.push_back(std::move(app));
    return out;
}

} // namespace gamehubnx
