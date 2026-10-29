#pragma once

#include "debrid_provider.hpp"

#include <cstdint>
#include <map>
#include <string>

namespace gamehubnx {

enum class CatalogFilter {
    All,
    Games,
};

enum class StreamSelection {
    AllFiles,
    PackagesOnly,
};

// Where stream installs commit content (PERF_PLAN 7.4). SystemMemory targets
// eMMC/NAND, whose write path is typically faster than SD.
enum class InstallLocation {
    SdCard,
    SystemMemory,
};

struct AppSettingsData {
    // UI language: "auto" follows the console's system language, otherwise a
    // borealis locale directory name. Read before Application::init() to set
    // Platform::APP_LOCALE_DEFAULT; borealis loads translations once, so a
    // change only takes effect on the next launch.
    std::string language = "auto";
    CatalogFilter catalogFilter = CatalogFilter::Games;
    // HTTPS URL to a switch_games.json-compatible catalog. Empty = built-in
    // Langegen source. Validated at parse time; a hand-edited settings.json
    // cannot smuggle a non-HTTPS or credential-bearing URL.
    std::string catalogSourceUrl;
    bool refreshCatalogOnLaunch = false;
    uint64_t lastCatalogRefreshMs = 0;
    // Wall-clock seconds of the last successful catalogue download. 0 = never
    // refreshed on this console (a bundled dump does not count). Used by the
    // freshness badge and the "not today" auto-refresh — distinct from
    // lastCatalogRefreshMs, which is monotonic now_ms() for the daily gate.
    uint64_t lastCatalogRefreshWallSec = 0;
    uint64_t lastMetadataRefreshMs = 0;
    uint64_t lastModsRefreshMs = 0;
    StreamSelection streamSelection = StreamSelection::AllFiles;
    InstallLocation installLocation = InstallLocation::SdCard;
    // Preguntar la carpeta cada vez que se descarga una ROM, en vez de dejarla
    // en la de tico sin mediar palabra. Por defecto NO se pregunta: quien tiene
    // tico quiere que la ROM aparezca ahi y nada mas, y preguntar en cada
    // descarga seria un modal de mas cada vez.
    bool askRomDestination = false;
    // Carpeta de ROMs por plataforma, slug -> ruta, solo para las que el
    // usuario haya cambiado. Un mapa y no un campo por plataforma porque las
    // plataformas las publica el servidor: manana puede haber una que hoy no
    // existe, y no deberia hacer falta tocar la app para poder colocarla.
    // Ausente = la ruta por defecto, que es lo que quiere casi todo el mundo.
    std::map<std::string, std::string> romPaths;
    bool showCompletedDownloads = true;
    bool extendedTelemetry = false;
    bool checkForUpdatesOnLaunch = true;
    // First-run disclaimer: catalog comes from a third party. Shown once.
    bool catalogDisclaimerAcknowledged = false;
    // Servidor FTP de SOLO LECTURA sobre la tarjeta, para sacar registros y
    // ficheros de la consola sin depender de otro homebrew.
    //
    // Apagado por defecto, al reves que el acompanante web al que sustituye: es
    // un servicio que publica la tarjeta entera en la red local, y eso no se
    // enciende sin que alguien lo pida. La clave gobierna igual --digitos, de 4
    // a 8, comprobado al leer para que un settings.json editado a mano no cuele
    // valores raros-- y se rellena sola con seis digitos si esta vacia, para
    // que encenderlo no lo deje abierto de par en par.
    bool ftpEnabled = false;
    std::string ftpPin;
    // How many torrents download at once. The default 1 is the serial
    // queue: nothing shares the link, so a single transfer runs at the speed
    // the swarm can actually give it. Raising it splits bandwidth, RAM budget
    // and SD throughput between tasks — opt-in, not something a stock install
    // does behind the user's back. Hand-edited values are clamped to the
    // supported range at parse time.
    uint32_t maxActiveDownloads = 1;
    bool firstRunCompleted = false;
    // Outbound proxy for HTTPS calls the app makes (catalog, artwork,
    // updates, the GameHub server). Empty = direct. Applied via ALL_PROXY, so
    // plain-HTTP requests to a LAN server would also be proxied — leave this
    // empty when the server is on the LAN, or point at a proxy that can reach
    // that host. Validated at parse time so a
    // hand-edited settings.json cannot smuggle an odd scheme.
    std::string proxyUrl;

    bool operator==(const AppSettingsData& other) const;
    bool operator!=(const AppSettingsData& other) const {
        return !(*this == other);
    }
};

// Supported values for AppSettingsData::language, in the order the Settings
// selector lists them. Anything else is rejected at parse time, so a hand-edited
// settings.json cannot leave the app pointing at a locale we do not ship.
inline constexpr const char* kLanguageValues[] = {
    "auto", "en-US", "ru", "pt-BR", "fr", "es", "zh-CN"};

bool isSupportedLanguage(const std::string& value);

// True for a valid web PIN: empty (caller will auto-generate) or 4-8 ASCII digits.
bool isValidWebPin(const std::string& value);

// Random 6-digit numeric PIN for the LAN companion.
std::string generateWebPin();

// Empty (direct) or scheme://host[:port] with scheme one of http, https,
// socks4, socks5, socks5h. curl accepts far more than that; this is the
// subset worth supporting on a console, and rejecting the rest early beats
// every request failing with a curl error nobody can read.
bool isValidProxyUrl(const std::string& value);

// Empty (built-in Langegen) or https:// with a non-empty host and path, no
// userinfo, max 512 chars — same rules enforced at parse and in the UI.
bool isValidCatalogSourceUrl(const std::string& value);

// User override or the built-in Langegen switch_games.json URL.
std::string effectiveCatalogSourceUrl(const std::string& custom);

// Points libcurl at the proxy for every handle the app creates, including
// ones already constructed — curl re-reads the environment per transfer, so
// a change takes effect on the next request rather than the next launch.
// Empty clears it. Global by design: threading a setting through nine
// independent curl_easy_init() call sites buys nothing over one env var.
void applyProxySetting(const std::string& proxyUrl);

// The supported range for AppSettingsData::maxActiveDownloads lives in
// download_manager.hpp: it is the engine's limit, and settings only validate
// against it. Include that header where you need clampMaxActiveDownloads.

bool dailyRefreshDue(uint64_t nowMs, uint64_t lastRefreshMs);

// True when `epochSec` falls on the local calendar day of `time(nullptr)`.
// epochSec <= 0 is never "today" (unknown / never refreshed).
bool isLocalToday(int64_t epochSec);

class AppSettings {
public:
    explicit AppSettings(std::string path,
                         std::string legacyTelemetryPath = {});

    bool load(std::string& error);
    bool update(const AppSettingsData& values, std::string& error);
    bool reset(std::string& error);

    const AppSettingsData& get() const { return values_; }
    uint64_t generation() const { return generation_; }
    const std::string& path() const { return path_; }

private:
    bool write(const AppSettingsData& values, std::string& error) const;

    std::string path_;
    std::string legacyTelemetryPath_;
    AppSettingsData values_;
    uint64_t generation_ = 0;
};

} // namespace gamehubnx
