// Fuente de homebrew: lectura de releases de GitHub y comparacion de versiones.
//
// Los JSON de aqui tienen la forma que devuelve /repos/{owner}/{repo}/releases.
// La parte de red (fetch y install) no se prueba sin conexion; el parser si.

#include "app/homebrew_source.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using gamehubnx::HomebrewApp;
using gamehubnx::HomebrewRelease;

// Dos releases publicados, uno en borrador y uno sin .nro. El borrador y el
// release sin .nro no deben aparecer nunca en la lista.
const char* kReleases = R"([
  {
    "tag_name": "v4.0.5", "draft": false, "prerelease": false,
    "published_at": "2026-10-19T01:41:25Z", "body": "Arreglos",
    "assets": [
      {"name": "source.zip", "size": 10,
       "browser_download_url": "https://example.test/source.zip"},
      {"name": "nextendo.nro", "size": 25044720,
       "browser_download_url": "https://example.test/nextendo.nro"}
    ]
  },
  {
    "tag_name": "v4.0.6-rc1", "draft": false, "prerelease": true,
    "published_at": "2026-10-20T10:00:00Z", "body": null,
    "assets": [
      {"name": "nextendo.NRO", "size": 25000000,
       "browser_download_url": "https://example.test/rc.nro"}
    ]
  },
  {
    "tag_name": "v9.9.9", "draft": true, "prerelease": false,
    "published_at": null, "body": "borrador",
    "assets": [
      {"name": "nextendo.nro", "size": 1,
       "browser_download_url": "https://example.test/draft.nro"}
    ]
  },
  {
    "tag_name": "v3.0.0", "draft": false, "prerelease": false,
    "published_at": "2026-01-01T00:00:00Z", "body": "",
    "assets": [
      {"name": "notes.txt", "size": 5,
       "browser_download_url": "https://example.test/notes.txt"}
    ]
  }
])";

void testParsesOnlyPublishedReleasesWithAnNro() {
    std::vector<HomebrewRelease> releases;
    std::string error;
    assert(gamehubnx::parseGithubReleases(kReleases, releases, error));
    // El borrador y el release sin .nro quedan fuera: quedan 2.
    assert(releases.size() == 2);

    const HomebrewRelease& stable = releases[0];
    assert(stable.tag == "v4.0.5");
    assert(stable.version == "4.0.5");
    assert(stable.nroName == "nextendo.nro");
    assert(stable.nroUrl == "https://example.test/nextendo.nro");
    assert(stable.nroSize == 25044720);
    assert(stable.notes == "Arreglos");
    assert(!stable.prerelease);

    // Un asset .NRO en mayusculas cuenta igual.
    const HomebrewRelease& rc = releases[1];
    assert(rc.tag == "v4.0.6-rc1");
    assert(rc.nroName == "nextendo.NRO");
    assert(rc.prerelease);
    // body null no rompe el parseo: queda vacio.
    assert(rc.notes.empty());
}

void testRejectsNonListJson() {
    std::vector<HomebrewRelease> releases;
    std::string error;
    assert(!gamehubnx::parseGithubReleases("{\"message\":\"Not Found\"}",
                                           releases, error));
    assert(!error.empty());
    assert(!gamehubnx::parseGithubReleases("no es json", releases, error));
}

void testRecommendedSkipsPrereleases() {
    std::vector<HomebrewRelease> releases;
    std::string error;
    assert(gamehubnx::parseGithubReleases(kReleases, releases, error));
    HomebrewApp app;
    app.releases = releases;
    // El primero es un prerelease: se recomienda el estable que sigue.
    const HomebrewRelease* pick = app.recommended();
    assert(pick && pick->tag == "v4.0.5");

    // Si todos son prerelease, se ofrece el mas reciente igualmente.
    app.releases = {releases[1]};
    assert(app.recommended() && app.recommended()->tag == "v4.0.6-rc1");

    app.releases.clear();
    assert(app.recommended() == nullptr);
}

void testVersionsCompareNumerically() {
    using gamehubnx::compareHomebrewVersions;
    // Lo que el string compara mal: 1.10.0 es mas nuevo que 1.9.2.
    assert(compareHomebrewVersions("1.10.0", "1.9.2") > 0);
    assert(compareHomebrewVersions("1.9.2", "1.10.0") < 0);
    // La "v" inicial no cuenta.
    assert(compareHomebrewVersions("v4.0.5", "4.0.5") == 0);
    // Un segmento que falta vale cero: 0.7 y 0.7.0 son iguales.
    assert(compareHomebrewVersions("0.7", "0.7.0") == 0);
    // El sufijo de prerelease no se compara: rc1 y la version final cuentan igual.
    assert(compareHomebrewVersions("4.0.6-rc1", "4.0.6") == 0);
    assert(compareHomebrewVersions("0.7.9", "0.7.8") > 0);
}

void testInstallPathFollowsHomebrewMenuLayout() {
    HomebrewRelease release;
    release.nroName = "tico.nro";
    assert(gamehubnx::homebrewNroPath(release) ==
           "sdmc:/switch/tico/tico.nro");
}


// El repo.json de HB App Store (fixture con tres paquetes reales y tres entradas
// que deben descartarse: nombre peligroso, binary fuera de switch/ y sin binary).
void testParsesHbRepoAndDropsUnsafeEntries() {
    std::ifstream in("tests/fixtures/homebrew/hb-repo-sample.json");
    std::stringstream ss;
    ss << in.rdbuf();
    std::vector<HomebrewApp> apps;
    std::string error;
    assert(gamehubnx::parseHbRepo(ss.str(), gamehubnx::homebrewSwitchRepoUrl(),
                                  apps, error));
    assert(apps.size() == 3);

    const HomebrewApp& app = apps[0];
    assert(app.repo == "100boxesNX");
    assert(app.displayName() == "100 Boxes NX");
    assert(app.author == "Cid2mizard");
    assert(app.releases.size() == 1);
    const HomebrewRelease& release = app.releases[0];
    assert(release.tag == "1.1");
    assert(release.isArchive());
    assert(release.archiveUrl ==
           "https://switch.cdn.fortheusers.org/zips/100boxesNX.zip");
    assert(release.archiveMd5 == "bbca286e0024be96b2762441d6d77cfb");
    assert(release.binaryPath == "/switch/100_Boxes_NX/100 Boxes NX.nro");
    assert(release.nroName == "100 Boxes NX.nro");
    assert(gamehubnx::homebrewNroPath(release) ==
           "sdmc:/switch/100_Boxes_NX/100 Boxes NX.nro");
}


// Forma exacta de /api/shop/homebrew: la app solo mira "repo" de cada entrada.
// Las entradas que no son "owner/name" valido se descartan, sin tumbar la lista.
void testParsesGameHubRepoList() {
    const char* json = R"({
      "version": 1,
      "repos": [
        {"repo": "NextendoNetwork/Prelude-Nextendo", "url": "https://github.com/NextendoNetwork/Prelude-Nextendo"},
        {"repo": "../evil", "url": "https://github.com/x"},
        {"repo": "https://evil.test/x"},
        {"repo": "a/b/c"},
        {"repo": "owner/name with space"},
        {"repo": "/sinowner"},
        {"repo": "dueno/"},
        {"url": "https://github.com/sin/repo"},
        {"repo": "luishidalgoa/green-nx"},
        {"repo": "luishidalgoa/green-nx"},
        "no es un objeto",
        {"repo": "ticohq/tico"}
      ]
    })";
    std::vector<std::string> repos;
    std::string error;
    assert(gamehubnx::parseHomebrewRepoList(json, repos, error));
    assert(repos.size() == 3);
    assert(repos[0] == "NextendoNetwork/Prelude-Nextendo");
    assert(repos[1] == "luishidalgoa/green-nx");   // el duplicado no cuenta dos veces
    assert(repos[2] == "ticohq/tico");
}

void testRejectsRepoListWithWrongShape() {
    std::vector<std::string> repos;
    std::string error;
    assert(!gamehubnx::parseHomebrewRepoList("[\"a/b\"]", repos, error));
    assert(!gamehubnx::parseHomebrewRepoList("{\"repos\": 3}", repos, error));
    assert(!gamehubnx::parseHomebrewRepoList("no es json", repos, error));
    assert(!error.empty());
}

void testRepoListUrlPointsAtGameHub() {
    assert(gamehubnx::homebrewReposUrl() ==
           "https://gamehub.hdglabs.com/api/shop/homebrew");
}

} // namespace

int main() {
    testParsesOnlyPublishedReleasesWithAnNro();
    testRejectsNonListJson();
    testRecommendedSkipsPrereleases();
    testVersionsCompareNumerically();
    testInstallPathFollowsHomebrewMenuLayout();
    testParsesHbRepoAndDropsUnsafeEntries();
    testParsesGameHubRepoList();
    testRejectsRepoListWithWrongShape();
    testRepoListUrlPointsAtGameHub();
    std::printf("test_homebrew_source: ok\n");
    return 0;
}
