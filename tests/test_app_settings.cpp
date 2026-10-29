#include "app/app_settings.hpp"
#include "app/gamehub_catalog.hpp"
#include "app/download_manager.hpp"  // clampMaxActiveDownloads

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <unistd.h>

using gamehubnx::AppSettings;
using gamehubnx::AppSettingsData;
using gamehubnx::CatalogFilter;
using gamehubnx::StreamSelection;
using gamehubnx::InstallLocation;
using gamehubnx::dailyRefreshDue;
using gamehubnx::isLocalToday;
using gamehubnx::isValidProxyUrl;
using gamehubnx::isValidCatalogSourceUrl;
using gamehubnx::effectiveCatalogSourceUrl;
using gamehubnx::applyProxySetting;

namespace {

const char* SettingsPath = "/tmp/gamehubnx-settings-test.json";
const char* LegacyPath = "/tmp/gamehubnx-settings-test.enabled";

void cleanup() {
    unlink(SettingsPath);
    unlink("/tmp/gamehubnx-settings-test.json.tmp");
    unlink(LegacyPath);
}

void testMissingFileUsesSafeDefaults() {
    cleanup();
    AppSettings settings(SettingsPath, LegacyPath);
    std::string error;
    assert(settings.load(error));
    const AppSettingsData& values = settings.get();
    assert(values.catalogFilter == CatalogFilter::Games);
    assert(!values.refreshCatalogOnLaunch);
    assert(values.lastCatalogRefreshMs == 0);
    assert(values.lastCatalogRefreshWallSec == 0);
    assert(values.lastMetadataRefreshMs == 0);
    assert(values.lastModsRefreshMs == 0);
    assert(values.streamSelection == StreamSelection::AllFiles);
    assert(values.installLocation == InstallLocation::SdCard);
    assert(values.showCompletedDownloads);
    assert(!values.extendedTelemetry);
    assert(values.checkForUpdatesOnLaunch);
    assert(values.maxActiveDownloads == 1);
    // "auto" keeps the console's system language, so a Russian Switch gets a
    // Russian UI on first launch with no user action.
    assert(values.language == "auto");
    assert(!values.catalogDisclaimerAcknowledged);
    assert(!values.firstRunCompleted);
    // Una instalacion nueva se lleva una clave de FTP al azar, para que
    // encender el servidor no lo deje abierto de par en par.
    assert(values.ftpPin.size() == 6);
    assert(gamehubnx::isValidWebPin(values.ftpPin));
    // Y el servidor viene APAGADO: publica la tarjeta entera en la red local,
    // asi que no se enciende sin que alguien lo pida.
    assert(!values.ftpEnabled);
    assert(access(SettingsPath, F_OK) == 0);
}

void testUpdatePersistsEveryPublicSetting() {
    cleanup();
    AppSettings settings(SettingsPath, LegacyPath);
    std::string error;
    assert(settings.load(error));
    AppSettingsData changed = settings.get();
    changed.language = "ru";
    changed.catalogFilter = CatalogFilter::All;
    changed.refreshCatalogOnLaunch = true;
    changed.lastCatalogRefreshMs = 123456;
    changed.lastCatalogRefreshWallSec = 1700000000;
    changed.lastMetadataRefreshMs = 234567;
    changed.lastModsRefreshMs = 345678;
    changed.streamSelection = StreamSelection::PackagesOnly;
    changed.installLocation = InstallLocation::SystemMemory;
    changed.showCompletedDownloads = false;
    changed.extendedTelemetry = true;
    changed.checkForUpdatesOnLaunch = false;
    changed.ftpEnabled = true;
    changed.ftpPin = "12345678";
    changed.maxActiveDownloads = 3;
    changed.catalogDisclaimerAcknowledged = true;
    changed.catalogSourceUrl =
        "https://cdn.example.com/repo/switch_games.json";
    changed.firstRunCompleted = true;
    assert(settings.update(changed, error));

    AppSettings restored(SettingsPath, LegacyPath);
    assert(restored.load(error));
    assert(restored.get() == changed);
}

void testOldSettingsJsonDefaultsRefreshTimes() {
    cleanup();
    {
        std::ofstream output(SettingsPath);
        output << "{"
               << "\"version\":1,"
               << "\"catalog_filter\":\"games\","
               << "\"refresh_catalog_on_launch\":true,"
               << "\"stream_selection\":\"all_files\","
               << "\"install_location\":\"sd_card\","
               << "\"show_completed_downloads\":true,"
               << "\"extended_telemetry\":false,"
               << "\"catalog_disclaimer_ack\":true"
               << "}";
    }
    AppSettings settings(SettingsPath, LegacyPath);
    std::string error;
    assert(settings.load(error));
    assert(settings.get().refreshCatalogOnLaunch);
    assert(settings.get().lastCatalogRefreshMs == 0);
    assert(settings.get().lastCatalogRefreshWallSec == 0);
    assert(settings.get().lastMetadataRefreshMs == 0);
    assert(settings.get().lastModsRefreshMs == 0);
    assert(settings.get().catalogDisclaimerAcknowledged);
    assert(settings.get().checkForUpdatesOnLaunch);
}

void testInvalidFileFailsClosedToDefaults() {
    cleanup();
    {
        std::ofstream output(SettingsPath);
        output << "{not-json";
    }
    AppSettings settings(SettingsPath, LegacyPath);
    std::string error;
    assert(!settings.load(error));
    assert(!error.empty());
    assert(settings.get() == AppSettingsData{});
}

// A hand-edited settings.json must not leave the app pointing at a locale we
// do not ship: borealis would log a load failure and fall back per-key, which
// reads as a half-translated UI rather than an error.
void testUnknownLanguageIsRejected() {
    cleanup();
    {
        std::ofstream output(SettingsPath);
        output << R"({"version":1,"language":"klingon"})";
    }
    AppSettings settings(SettingsPath, LegacyPath);
    std::string error;
    assert(!settings.load(error));
    assert(!error.empty());
    assert(settings.get() == AppSettingsData{});

    for (const char* supported : gamehubnx::kLanguageValues)
        assert(gamehubnx::isSupportedLanguage(supported));
    assert(!gamehubnx::isSupportedLanguage("klingon"));
}

void testLegacyTelemetryFlagMigratesOnce() {
    cleanup();
    {
        std::ofstream output(LegacyPath);
        output << "enabled\n";
    }
    AppSettings settings(SettingsPath, LegacyPath);
    std::string error;
    assert(settings.load(error));
    assert(settings.get().extendedTelemetry);
    assert(!settings.get().ftpPin.empty());
    assert(access(SettingsPath, F_OK) == 0);
    assert(access(LegacyPath, F_OK) != 0);
}

// Una clave editada a mano que no sean de 4 a 8 digitos se LIMPIA: dejarla
// puesta seria una clave que no funciona. No se sustituye por otra, porque
// quedarse sin clave es un estado legitimo -- el FTP puede querer dejarse
// abierto en la red local.
void testInvalidWebPinIsCleared() {
    cleanup();
    {
        std::ofstream output(SettingsPath);
        output << R"({"version":1,"ftp_pin":"letters"})";
    }
    AppSettings settings(SettingsPath, LegacyPath);
    std::string error;
    assert(settings.load(error));
    assert(settings.get().ftpPin.empty());
    // Y sigue apagado.
    assert(!settings.get().ftpEnabled);

    assert(gamehubnx::isValidWebPin(""));
    assert(gamehubnx::isValidWebPin("1234"));
    assert(gamehubnx::isValidWebPin("12345678"));
    assert(!gamehubnx::isValidWebPin("123"));
    assert(!gamehubnx::isValidWebPin("123456789"));
    assert(!gamehubnx::isValidWebPin("12a4"));

    const std::string generated = gamehubnx::generateWebPin();
    assert(generated.size() == 6);
    assert(gamehubnx::isValidWebPin(generated));
}

// Borrar la clave se GUARDA vacia. Antes se regeneraba una, y eso hacia
// imposible dejar el FTP sin clave: quien lo enciende decide si quiere que sea
// abierto en su red o no, y la aplicacion no puede deshacer esa eleccion.
void testUpdateEmptyPinIsKept() {
    cleanup();
    AppSettings settings(SettingsPath, LegacyPath);
    std::string error;
    assert(settings.load(error));
    AppSettingsData values = settings.get();
    // Instalacion nueva: sale con clave puesta, para que encenderlo no lo deje
    // abierto sin querer.
    assert(values.ftpPin.size() == 6);
    values.ftpPin.clear();
    assert(settings.update(values, error));
    assert(settings.get().ftpPin.empty());
    // Y sobrevive a releerlo del disco.
    AppSettings recargado(SettingsPath, LegacyPath);
    assert(recargado.load(error));
    assert(recargado.get().ftpPin.empty());
}

// A hand-edited count outside [1,4] degrades to the nearest supported value
// rather than failing the whole settings load.
void testMaxActiveDownloadsClamped() {
    cleanup();
    {
        std::ofstream output(SettingsPath);
        output << R"({"version":2,"max_active_downloads":99})";
    }
    AppSettings settings(SettingsPath, LegacyPath);
    std::string error;
    assert(settings.load(error));
    assert(settings.get().maxActiveDownloads == gamehubnx::kMaxActiveDownloads);

    assert(gamehubnx::clampMaxActiveDownloads(0) == 1);
    assert(gamehubnx::clampMaxActiveDownloads(1) == 1);
    assert(gamehubnx::clampMaxActiveDownloads(4) == 4);
    assert(gamehubnx::clampMaxActiveDownloads(5) == 4);
    assert(gamehubnx::clampMaxActiveDownloads(UINT64_MAX) == 4);
}

// v1 -> v2: every stored download count goes back to the serial queue, and
// everything else in the file comes through untouched. Raising the count
// afterwards has to survive a restart — the update() that persists it also
// stamps the new version, which is what ends the reset.
void testVersionOneResetsActiveDownloads() {
    cleanup();
    {
        std::ofstream output(SettingsPath);
        output << R"({"version":1,"max_active_downloads":4,)"
               << R"("language":"ru","ftp_pin":"4242"})";
    }
    {
        AppSettings settings(SettingsPath, LegacyPath);
        std::string error;
        assert(settings.load(error));
        assert(settings.get().maxActiveDownloads == 1);
        assert(settings.get().language == "ru");
        assert(settings.get().ftpPin == "4242");
    }
    // update() stamps the new version, so a deliberate 4 now sticks.
    {
        AppSettings settings(SettingsPath, LegacyPath);
        std::string error;
        assert(settings.load(error));
        AppSettingsData values = settings.get();
        values.maxActiveDownloads = 4;
        assert(settings.update(values, error));
    }
    {
        AppSettings settings(SettingsPath, LegacyPath);
        std::string error;
        assert(settings.load(error));
        assert(settings.get().maxActiveDownloads == 4);
    }
}

// A file from a build newer than this one is not something we can safely
// reinterpret, so it fails closed instead of being silently downgraded.
void testFutureVersionIsRejected() {
    cleanup();
    {
        std::ofstream output(SettingsPath);
        output << R"({"version":99,"language":"ru"})";
    }
    AppSettings settings(SettingsPath, LegacyPath);
    std::string error;
    assert(!settings.load(error));
    assert(!error.empty());
}

void testDailyRefreshDue() {
    const uint64_t day = 24ULL * 60ULL * 60ULL * 1000ULL;
    assert(dailyRefreshDue(1000, 0));
    assert(!dailyRefreshDue(day + 999, 1000));
    assert(dailyRefreshDue(day + 1000, 1000));
    assert(dailyRefreshDue(999, 1000));
}

void testIsLocalToday() {
    assert(!isLocalToday(0));
    assert(!isLocalToday(-1));
    assert(isLocalToday(static_cast<int64_t>(time(nullptr))));
    assert(!isLocalToday(static_cast<int64_t>(time(nullptr)) - 48 * 60 * 60));
}

// Files from the versions with Real-Debrid, TorBox, TorrServer and the torrent
// switch still carry those keys. They are ignored, and the file still loads.
void testLegacyDebridKeysTolerated() {
    cleanup();
    {
        std::ofstream output(SettingsPath);
        output << "{\"version\":3,\"torrenting_enabled\":true,"
               << "\"torbox_api_key\":\"abc\",\"debrid_provider\":\"torrserver\","
               << "\"web_server_enabled\":true}";
    }
    AppSettings settings(SettingsPath, LegacyPath);
    std::string error;
    assert(settings.load(error));
    assert(settings.get().firstRunCompleted == false);
}

void testProxyUrlValidation() {
    assert(isValidProxyUrl(""));
    assert(isValidProxyUrl("socks5://192.168.1.2:10808"));
    assert(isValidProxyUrl("socks5h://proxy.lan:1080"));
    assert(isValidProxyUrl("http://10.0.0.1:3128"));
    assert(isValidProxyUrl("https://proxy.example.com"));
    // No scheme, unsupported scheme, or anything past host:port.
    assert(!isValidProxyUrl("192.168.1.2:10808"));
    assert(!isValidProxyUrl("ftp://proxy:21"));
    assert(!isValidProxyUrl("socks5://proxy:1080/path"));
    assert(!isValidProxyUrl("socks5://user@proxy:1080"));
    assert(!isValidProxyUrl("socks5://proxy:0"));
    assert(!isValidProxyUrl("socks5://proxy:70000"));
    assert(!isValidProxyUrl("socks5://proxy:abc"));
    assert(!isValidProxyUrl("socks5://"));
    assert(!isValidProxyUrl("socks5://host with space:1080"));
}

// A hand-edited settings.json must not be able to smuggle a proxy the
// validator would reject through the parser.
void testInvalidProxyUrlIsCleared() {
    cleanup();
    {
        std::ofstream output(SettingsPath);
        output << R"({"version":3,"proxy_url":"ftp://nope:21"})";
    }
    AppSettings settings(SettingsPath, LegacyPath);
    std::string error;
    assert(settings.load(error));
    assert(settings.get().proxyUrl.empty());
}

void testInvalidCatalogSourceUrlIsCleared() {
    cleanup();
    {
        std::ofstream output(SettingsPath);
        output << R"({"version":3,"catalog_source_url":"ftp://nope/catalog.json"})";
    }
    AppSettings settings(SettingsPath, LegacyPath);
    std::string error;
    assert(settings.load(error));
    assert(settings.get().catalogSourceUrl.empty());
}

void testCatalogSourceUrlValidation() {
    assert(isValidCatalogSourceUrl(""));
    assert(isValidCatalogSourceUrl(
        "https://cdn.example.com/repo/catalog.json"));
    assert(!isValidCatalogSourceUrl(
        "http://cdn.example.com/repo/catalog.json"));
    assert(!isValidCatalogSourceUrl("https://"));
    assert(!isValidCatalogSourceUrl(
        "https://user:pass@cdn.example.com/repo/catalog.json"));
    assert(!isValidCatalogSourceUrl(std::string(513, 'a')));
    // FORK DIVERGENCE. Upstream defaults to the Langegen torrent catalogue;
    // this fork exists to serve one self-hosted GameHub library and shows it
    // out of the box, with nothing to configure. Only the *default* moves:
    // kDefaultCatalogSourceUrl still names the upstream catalogue, which stays
    // reachable by setting it as a custom source. Expect this line to conflict
    // on merge — keeping the fork's value is the correct resolution.
    // DIVERGENCIA DEL FORK. Aqui la fuente NO es configurable: siempre es la
    // tienda propia, tambien cuando los ajustes traen una URL guardada. Antes se
    // respetaba la personalizada, y bastaba con que una instalacion anterior
    // hubiese dejado ahi la de upstream para que la app siguiera mostrando su
    // catalogo de torrents pese al cambio de defecto -- fue exactamente lo que
    // le paso al usuario. Al fusionar con upstream, la resolucion correcta es
    // conservar estas dos lineas.
    assert(effectiveCatalogSourceUrl("") == gamehubnx::gamehub::kCatalogUrl);
    assert(effectiveCatalogSourceUrl("https://cdn.example.com/x.json") ==
           gamehubnx::gamehub::kCatalogUrl);
}

void testProxySettingReachesEnvironment() {
    applyProxySetting("socks5://192.168.1.2:10808");
    const char* value = std::getenv("ALL_PROXY");
    assert(value && std::string(value) == "socks5://192.168.1.2:10808");
    const char* noProxy = std::getenv("NO_PROXY");
    assert(noProxy && std::string(noProxy).find("127.0.0.1") != std::string::npos);
    applyProxySetting("");
    assert(std::getenv("ALL_PROXY") == nullptr);
    assert(std::getenv("NO_PROXY") == nullptr);
}

} // namespace

int main() {
    testMissingFileUsesSafeDefaults();
    testUpdatePersistsEveryPublicSetting();
    testOldSettingsJsonDefaultsRefreshTimes();
    testInvalidFileFailsClosedToDefaults();
    testUnknownLanguageIsRejected();
    testLegacyTelemetryFlagMigratesOnce();
    testInvalidWebPinIsCleared();
    testUpdateEmptyPinIsKept();
    testMaxActiveDownloadsClamped();
    testVersionOneResetsActiveDownloads();
    testFutureVersionIsRejected();
    testLegacyDebridKeysTolerated();
    testProxyUrlValidation();
    testInvalidProxyUrlIsCleared();
    testInvalidCatalogSourceUrlIsCleared();
    testCatalogSourceUrlValidation();
    testProxySettingReachesEnvironment();
    testDailyRefreshDue();
    testIsLocalToday();
    cleanup();
    std::puts("app settings tests passed");
    return 0;
}
