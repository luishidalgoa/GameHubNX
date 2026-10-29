#include "app/app_paths.h"
#include "app/app_settings.hpp"
#include "app/catalog_service.hpp"
#include "app/gamehub_catalog.hpp"
#include "app/download_manager.hpp"
#include "app/ftp_server.hpp"
#include "app/game_metadata_service.hpp"
#include "app/game_update_service.hpp"
#include "app/installed_title_service.hpp"
#include "app/shop_update_source.hpp"
#include "app/switch_deploy.hpp"
#include "app/update_service.hpp"
#include "platform/switch_crashlog.h"
#include "platform/switch_performance.hpp"

extern "C" {
#include "core/dht.h"
#include "core/util.h"
}

#include <borealis.hpp>
#include <curl/curl.h>
#include <switch.h>
#include <switch-ipcext.h>

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

#include "app/mod_index_service.hpp"
#include "ui/catalog/catalog_view.hpp"
#include "ui/common/burn_in_saver.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/common/ftp_address.hpp"
#include "ui/i18n.hpp"
#include "ui/files/file_browser_view.hpp"
#include "ui/main_frame.hpp"
#include "ui/platforms/platforms_view.hpp"
#include "ui/homebrew/homebrew_view.hpp"
#include "ui/downloads/downloads_view.hpp"
#include "ui/downloads/task_files_activity.hpp"
#include "ui/installed/installed_view.hpp"
#include "ui/settings/about_view.hpp"
#include "ui/settings/help_view.hpp"
#include "ui/settings/settings_view.hpp"
#include "ui/theme.hpp"

using gamehubnx::AppSettings;
using gamehubnx::CatalogService;
using gamehubnx::DownloadManager;
using gamehubnx::SwitchDeployService;
using gamehubnx::GameMetadataService;
using gamehubnx::GameUpdateService;
using gamehubnx::InstalledTitleService;
using gamehubnx::FavoritesService;
using gamehubnx::ModIndexService;
using gamehubnx::SwitchPerformanceController;
using gamehubnx::UpdateCheckResult;
using gamehubnx::UpdateService;
using gamehubnx::FtpServer;

using namespace gamehubnx::ui;

namespace {

constexpr auto kCompanionSettingsTimeout = std::chrono::seconds(5);

bool runOnUiThread(const std::function<void()>& fn) {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    brls::sync([&] {
        fn();
        std::lock_guard<std::mutex> lock(mutex);
        done = true;
        cv.notify_one();
    });
    std::unique_lock<std::mutex> lock(mutex);
    return cv.wait_for(lock, kCompanionSettingsTimeout, [&] { return done; });
}

// Sin catalogo empotrado, A PROPOSITO.
//
// El proyecto del que viene este fork empaqueta en la ROM un volcado del
// catalogo de torrents de Langegen/RuTracker, y CatalogService lo pinta cuando
// aun no hay nada descargado. Aqui eso significaba abrir la app y ver el
// catalogo de otro producto en lugar de la tienda propia -- que es exactamente
// lo que reporto el usuario.
//
// Vacio quiere decir: hasta que la tienda responda, no se ensena un catalogo;
// se ensena que se esta cargando. Es preferible a mostrar contenido ajeno.
constexpr const char* BundledCatalogPath = "";

// AppSettingsData::language -> the borealis locale to load. LOCALE_AUTO makes
// SwitchPlatform read the console's system language, so a Russian console gets
// a Russian UI with no user action; anything we do not ship a locale directory
// for falls back to en-US per key.
// Joins on scope exit so an exception between spawn and the explicit join
// unwinds cleanly instead of hitting std::terminate in ~thread().
struct ThreadJoiner {
    std::thread thread;
    ~ThreadJoiner() {
        if (thread.joinable())
            thread.join();
    }
};

const std::string& borealisLocaleFor(const std::string& language) {
    if (language == "ru")
        return brls::LOCALE_RU;
    if (language == "en-US")
        return brls::LOCALE_EN_US;
    if (language == "pt-BR")
        return brls::LOCALE_PT_BR;
    if (language == "fr")
        return brls::LOCALE_FR;
    if (language == "es")
        return brls::LOCALE_ES;
    if (language == "zh-CN")
        return brls::LOCALE_ZH_CN;
    return brls::LOCALE_AUTO;
}

class MainActivity : public brls::Activity {
public:
    MainActivity(DownloadManager* manager, CatalogService* catalog,
                 GameMetadataService* metadata,
                 InstalledTitleService* installed, AppSettings* settings,
                 UpdateService* updater, ModIndexService* mods,
                 FavoritesService* favorites, FtpServer* ftpServer,
                 SwitchDeployService* deploy, GameUpdateService* gameUpdates)
        : manager_(manager), catalog_(catalog), metadata_(metadata),
          installed_(installed), settings_(settings), updater_(updater),
          mods_(mods), favorites_(favorites), ftpServer_(ftpServer),
          deploy_(deploy), gameUpdates_(gameUpdates) {
        auto* tabs = new gamehubnx::ui::MainFrame();
        using gamehubnx::ui::NavIconType;
        tabs->addNavTab(tr("gamehubnx/nav/catalog"), NavIconType::Catalog,
                        [manager, catalog, metadata, installed,
                         settings, mods, favorites, deploy, tabs] {
            return new CatalogView(manager, catalog, metadata, installed,
                                   // Indice 2 = Descargas. Plataformas se
                                   // metio en medio y corrio esta pestana un
                                   // puesto; con el 1 se abria Plataformas.
                                   settings, [tabs] { tabs->focusTab(2); },
                                   mods, favorites, deploy);
        });
        tabs->addNavTab(tr("gamehubnx/platforms/title"),
                        NavIconType::Platforms,
                        [catalog, metadata, manager, settings, favorites] {
            return new gamehubnx::ui::PlatformsView(catalog, metadata, manager,
                                                    settings, favorites);
        });
        tabs->addNavTab(tr("gamehubnx/nav/downloads"), NavIconType::Downloads,
                        [manager, metadata, settings, deploy] {
            return new MainView(manager, metadata, settings, deploy);
        });
        tabs->addNavTab(tr("gamehubnx/nav/installed"), NavIconType::Installed,
                        [installed, manager, metadata, settings, catalog,
                         gameUpdates] {
            return new InstalledView(installed, manager, metadata, settings,
                                     catalog, gameUpdates);
        });
        tabs->addNavTab(tr("gamehubnx/nav/updates"), NavIconType::Updates,
                        [installed, manager, metadata, settings, catalog,
                         gameUpdates] {
            return new InstalledView(installed, manager, metadata, settings,
                                     catalog, gameUpdates, true,
                                     InstalledView::Mode::Updates);
        });
        // El explorador, con pestana propia. Estuvo enterrado el octavo en
        // Ajustes y no se encontraba: dos veces se reporto que no estaba,
        // estandolo. Aqui cabe porque la barra lateral ya reserva el alto del
        // pie (kSidebarFooterReserve); sin eso, una septima entrada se metia
        // debajo de la IP.
        //
        // Arranca en la raiz de la tarjeta y no en la carpeta de descargas: lo
        // que hay que recolocar suele estar justamente fuera de ella.
        tabs->addNavTab(tr("gamehubnx/browser/tab"), NavIconType::Files,
                        [] { return new FileBrowserView("sdmc:/"); });
        // Homebrew: los .nro de los releases de GitHub de los repos conocidos.
        // Sin catalogo propio: la lista sale de GitHub en cada apertura.
        tabs->addNavTab(tr("gamehubnx/nav/homebrew"), NavIconType::Store,
                        [] { return new gamehubnx::ui::HomebrewView(); });
        tabs->addNavTab(tr("gamehubnx/nav/settings"), NavIconType::Settings,
                        [settings, manager, catalog, metadata,
                         installed, updater, mods, ftpServer] {
            return new SettingsView(settings, manager, catalog, metadata,
                                    installed, updater, mods, ftpServer);
        });
        // Ayuda y Acerca-de no se registran: la primera explica torrents,
        // proveedores de debrid y magnets -- nada de lo que hace esta app -- y
        // la segunda es la ficha del proyecto base. Las vistas siguen
        // compiladas y sin tocar para que los merges con upstream no choquen;
        // lo unico que cambia es que no aparecen en la navegacion.
        tabs->attachStorageFooter(manager, ftpServer);
        frame_ = new brls::AppletFrame(tabs);
        frame_->setTitle(tr("gamehubnx/app/title"));
    }

    brls::View* createContentView() override {
        return frame_;
    }

    void onContentAvailable() override {
        // Hidden hint: this action sits on the frame, so its label would ride
        // the bottom bar on every screen under MainActivity — and the catalog
        // already registers more hints than a 1280px bar holds in Russian.
        // Plus-to-exit is a console convention, and HOME works regardless.
        registerAction(tr("gamehubnx/app/exit"), brls::BUTTON_START,
            [this](brls::View*) {
                startupStage("quit requested by Plus");
                if (deploy_ && deploy_->snapshot().active()) {
                    auto* dialog = new brls::Dialog(
                        tr("gamehubnx/deploy/exit_question"));
                    dialog->addButton(tr("gamehubnx/common/cancel"), [] {});
                    dialog->addButton(tr("gamehubnx/deploy/cancel_and_exit"),
                                      [this] {
                        deploy_->cancel();
                        brls::Application::quit();
                    });
                    dialog->open();
                    return true;
                }
                brls::Application::quit();
                return true;
            }, /*hidden=*/true);
        // La direccion del FTP, desde cualquier pantalla. Sin QR: un codigo
        // servia para que un movil abriera una pagina; esto se teclea en un
        // cliente de FTP desde un ordenador, asi que lo util es el texto.
        registerAction(tr("gamehubnx/ftp/address"), brls::BUTTON_BACK,
            [this](brls::View*) {
                const gamehubnx::AppSettingsData& values = settings_->get();
                const std::string url =
                    gamehubnx::ui::ftpAddress(ftpServer_, values.ftpEnabled);
                if (url.empty()) {
                    brls::Application::notify(
                        tr(values.ftpEnabled ? "gamehubnx/ftp/no_address"
                                             : "gamehubnx/ftp/off"));
                    return true;
                }
                // La clave va aparte: ensenarla junto a la direccion la deja a
                // la vista de cualquiera que pase por delante de la tele.
                brls::Application::notify(url);
                return true;
            });
    }

private:
    DownloadManager* manager_;
    CatalogService* catalog_;
    GameMetadataService* metadata_;
    InstalledTitleService* installed_;
    AppSettings* settings_;
    UpdateService* updater_;
    ModIndexService* mods_;
    FavoritesService* favorites_;
    FtpServer* ftpServer_;
    SwitchDeployService* deploy_;
    GameUpdateService* gameUpdates_;
    brls::AppletFrame* frame_;
};

}  // namespace

int main(int argc, char** argv) {
    // A library applet must only terminate after qlaunch asks it to close.
    // Keep this path before logging, settings, and custom signal handlers so
    // the unsupported mode uses only libnx's normal applet lifecycle.
    if (!isApplicationMode()) {
        showApplicationModeRequired();
        return 0;
    }

    switch_crashlog_install();
    switch_crashlog_stage("creating application directories");
    mkdir(GHNX_SWITCH_ROOT, 0755);
    mkdir(GHNX_APP_ROOT, 0755);
    log_init(LogPath);

    (void)argc;
    (void)argv;
    UpdateService launchUpdater;
    const bool updatePendingConfirmation =
        launchUpdater.hasPendingConfirmation();
    // A verified download staged by a previous session that quit before the
    // helper finished the swap. Do NOT auto-chain to the helper here: an
    // unconditional envSetNextLoad + quit on every launch turns a single failed
    // helper load into a crash loop that bricks the app. It is surfaced as a
    // user-triggered "install now?" prompt once the UI is up (see below).
    const bool updatePendingFinish = launchUpdater.stagedReady();
    AppSettings settings(SettingsPath, TelemetryFlagPath);
    std::string settingsError;
    if (!settings.load(settingsError))
        diagnostic_error("settings", "startup", "error=%s",
                         settingsError.c_str());
    telemetry_set_enabled(settings.get().extendedTelemetry ? 1 : 0);
    // Identificador de esta instalacion: GameHub lo recibe en cada peticion y asi
    // puede distinguir las consolas. Se crea una vez y se queda en la SD.
    gamehubnx::gamehub::setInstallDeviceId(
        gamehubnx::gamehub::loadOrCreateDeviceId(GHNX_PATH("device_id"), rand_bytes));
    // Before curl_global_init and before any service builds a handle: curl
    // reads the proxy from the environment when it sets up a transfer.
    gamehubnx::applyProxySetting(settings.get().proxyUrl);
    if (!settings.get().proxyUrl.empty())
        log_msg("[startup] proxy %s\n", settings.get().proxyUrl.c_str());
    log_msg("[telemetry] setting enabled=%d interval_ms=5000 build='%s %s'\n",
            telemetry_enabled(), __DATE__, __TIME__);
    startupStage("entered main");

    openBorealisLog();

    std::set_terminate([] {
        switch_crashlog_stage("uncaught C++ exception");
        // "std::terminate called" on its own names nothing. Rethrowing the
        // in-flight exception is the only way to get its type and message
        // into the log, and it is safe here: we _Exit either way.
        if (std::exception_ptr current = std::current_exception()) {
            try {
                std::rethrow_exception(current);
            } catch (const std::exception& e) {
                log_msg("[crash] std::terminate: %s: %s\n",
                        typeid(e).name(), e.what());
            } catch (...) {
                log_msg("[crash] std::terminate: non-std exception\n");
            }
        } else {
            log_msg("[crash] std::terminate called with no live exception\n");
        }
        log_flush();
        std::_Exit(134);
    });

    bool curlReady = false;
    bool ncmReady = false;
    bool nsReady = false;
    bool esReady = false;
    try {
        log_msg("[startup] applet_type=%d operation_mode=%d\n",
                (int)appletGetAppletType(), (int)appletGetOperationMode());

        // Our own sockets pass MSG_NOSIGNAL, and every curl handle now sets
        // CURLOPT_NOSIGNAL — which also stops libcurl asking the system to
        // ignore SIGPIPE. Do it here instead, as main_pc.c already does.
        signal(SIGPIPE, SIG_IGN);

        startupStage("curl_global_init");
        CURLcode curlResult = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (curlResult != CURLE_OK) {
            log_msg("[startup] curl_global_init failed: %d\n",
                    (int)curlResult);
            throw std::runtime_error("curl_global_init failed");
        }
        curlReady = true;

        startupStage("installer services");
        Result rc = ncmInitialize();
        if (R_FAILED(rc))
            throw std::runtime_error("ncmInitialize failed");
        ncmReady = true;
        rc = nsInitialize();
        if (R_FAILED(rc))
            throw std::runtime_error("nsInitialize failed");
        nsReady = true;
        rc = esInitialize();
        if (R_FAILED(rc))
            throw std::runtime_error("esInitialize failed");
        esReady = true;

        startupStage("Borealis Application::init");
        // Must precede init(): the platform captures the locale in its
        // constructor and Application::init() loads translations exactly once,
        // which is why a language change only lands on the next launch.
        brls::Platform::APP_LOCALE_DEFAULT =
            borealisLocaleFor(settings.get().language);
        if (!brls::Application::init())
            throw std::runtime_error("Borealis Application::init failed");
        gamehubnx::ui::theme::registerColors();
        gamehubnx::ui::installSidebarStyle();

        startupStage("Borealis createWindow");
        brls::Application::createWindow("gamehubnx");
        brls::Application::setGlobalQuit(false);

        startupStage("CatalogService construction");
        log_msg("[startup] image relay: relays-first + disk cache (rev4)\n");
        unlink(GHNX_PATH("rutracker.cfg"));
        unlink(GHNX_PATH("rutracker_cookies.txt"));
        CatalogService catalog(GHNX_APP_ROOT, BundledCatalogPath);

        // The metadata index parse (an ~8 MB JSON) runs on a worker thread in
        // parallel with the catalog parse below; the service is not touched by
        // anything else until the join before MainActivity construction, after
        // which all access is UI-thread as before. Startup pays
        // max(catalog, metadata) instead of their sum.
        startupStage("GameMetadataService construction");
        GameMetadataService metadata(GHNX_APP_ROOT);
        std::string metadataError;
        bool metadataOk = true;
        ThreadJoiner metadataLoader{
            std::thread([&metadata, &metadataError, &metadataOk] {
                metadataOk = metadata.load(metadataError);
            })};

        std::string catalogError;
        if (!catalog.load(catalogError))
            log_msg("[catalog] initial load failed: %s\n",
                    catalogError.c_str());

        startupStage("ModIndexService construction");
        ModIndexService mods(GHNX_APP_ROOT);
        std::string modsError;
        if (!mods.load(modsError))
            log_msg("[mods] initial load skipped: %s\n", modsError.c_str());

        startupStage("FavoritesService construction");
        FavoritesService favorites(GHNX_APP_ROOT);
        std::string favoritesError;
        if (!favorites.load(favoritesError))
            log_msg("[favorites] initial load skipped: %s\n",
                    favoritesError.c_str());

        // The installed-title scan does one full control-data IPC read (NACP +
        // up to 128 KB icon) per installed title — on a full console this was
        // the single largest startup cost, all before the first frame. The
        // service is internally locked and the UI already refreshes it via
        // brls::async, so run the initial scan on its own thread and let the
        // UI come up immediately; the list fills in when the scan lands.
        startupStage("InstalledTitleService refresh (async)");
        InstalledTitleService installed(GHNX_APP_ROOT);
        ThreadJoiner installedScanner{std::thread([&installed] {
            std::string installedError;
            if (!installed.refresh(installedError))
                diagnostic_error("installed", "startup", "error=%s",
                                 installedError.c_str());
        })};

        startupStage("DownloadManager construction");
        SwitchPerformanceController performance;
        dht_engine_set_cache_path(GHNX_PATH("dht.cache"));
        DownloadManager manager(GHNX_APP_ROOT);
        SwitchDeployService deploy(manager, GHNX_APP_ROOT,
                                   GHNX_SWITCH_ROOT);
        manager.setInstallTarget(
            installTargetFor(settings.get().installLocation));
        manager.setMaxActiveDownloads(settings.get().maxActiveDownloads);
        metadata.setImageNetwork(
            manager.hasActiveTransfer()
                ? GameMetadataService::ImageNetwork::Throttled
                : GameMetadataService::ImageNetwork::Full);

        UpdateService updater;

        startupStage("FtpServer construction");
        // Solo lectura y apagado por defecto: publica la tarjeta entera en la
        // red local, y eso no se enciende sin que alguien lo pida.
        gamehubnx::FtpServer ftpServer("sdmc:/", settings.get().ftpPin);
        if (settings.get().ftpEnabled) {
            std::string ftpError;
            if (!ftpServer.start(gamehubnx::FtpServer::kDefaultPort, ftpError))
                log_msg("[ftp] no se pudo arrancar: %s\n", ftpError.c_str());
        }

        // Barrier: from here on the UI reads the metadata service, so the
        // parallel index parse must have landed.
        startupStage("join metadata loader");
        metadataLoader.thread.join();
        if (!metadataOk)
            log_msg("[metadata] initial load failed: %s\n",
                    metadataError.c_str());

        startupStage("GameUpdateService load");
        // La tienda es la unica fuente de versiones nuevas (shop_update_source.hpp).
        gamehubnx::ShopUpdateSource shopUpdates(&catalog);
        GameUpdateService gameUpdates(&shopUpdates,
                                      GHNX_PATH("game-updates.json"));
        std::string gameUpdatesError;
        if (!gameUpdates.load(gameUpdatesError))
            diagnostic_error("game_updates", "load", "error=%s",
                             gameUpdatesError.c_str());

        startupStage("MainActivity construction");
        auto* activity = new MainActivity(&manager, &catalog, &metadata,
                                           &installed, &settings, &updater,
                                           &mods, &favorites, &ftpServer,
                                           &deploy, &gameUpdates);

        // Diagnostico del recuadro de foco mal dibujado (issue #15/#25). El
        // recuadro se pinta con getX/getY/getWidth/getHeight de la vista
        // enfocada (borealis view.cpp:545), asi que con la clase y el marco de
        // cada cambio de foco se distingue "el foco esta en otra vista" de "la
        // geometria de esa vista es la que no cuadra". Una linea por cambio de
        // foco, solo cuando el log de diagnostico esta activo.
        brls::Application::getGlobalFocusChangeEvent()->subscribe(
            [](brls::View* view) {
                if (!view)
                    return;
                log_msg("[focus] %s x=%.0f y=%.0f w=%.0f h=%.0f\n",
                        view->describe().c_str(), view->getX(), view->getY(),
                        view->getWidth(), view->getHeight());
            });

        startupStage("push MainActivity");
        brls::Application::pushActivity(activity);
        if (updatePendingFinish) {
            // Finish an update staged before a previous quit. User-triggered so
            // a helper that fails to load can never become an automatic loop.
            startupStage("pending update prompt");
            const std::string helper = launchUpdater.helperPath();
            auto* dialog = new brls::Dialog(
                tr("gamehubnx/settings/update_pending_install"));
            dialog->addButton(tr("gamehubnx/settings/install_and_restart"),
                              [helper] {
#ifdef __SWITCH__
                if (!envHasNextLoad()) {
                    brls::Application::notify(
                        tr("gamehubnx/settings/update_no_restart"));
                    return;
                }
                const std::string arguments =
                    "\"" + helper + "\" --finish-update";
                const Result result = envSetNextLoad(helper.c_str(),
                                                     arguments.c_str());
                if (R_FAILED(result)) {
                    brls::Application::notify(
                        tr("gamehubnx/settings/update_restart_failed"));
                    return;
                }
#endif
                brls::Application::quit();
            });
            dialog->addButton(tr("gamehubnx/common/later"), [] {});
            dialog->open();
        }
        if (!updatePendingFinish && settings.get().checkForUpdatesOnLaunch) {
            updater.checkAsync([](UpdateCheckResult result) {
                if (!result.ok || !result.updateAvailable)
                    return;
                brls::sync([version = std::move(result.release.version)] {
                    brls::Application::notify(
                        tr("gamehubnx/settings/update_available_launch", version));
                });
            });
        }

        // GameHubNX entra DIRECTO a la tienda. El primer arranque de la app
        // base encadena tres pantallas antes del catalogo -- eleccion de
        // metodo (torrent o proveedor de debrid), aviso legal sobre catalogos
        // de terceros, y vinculacion de la cuenta del proveedor -- y ninguna
        // significa nada aqui: no hay metodo que elegir porque siempre es la
        // tienda propia por HTTPS, el catalogo no es de un tercero sino del
        // servidor del usuario, y no hay cuenta que vincular.
        //
        // Se marcan como resueltas en vez de borrar el codigo: las pantallas
        // siguen ahi para cuando upstream las cambie, y esto es una sola
        // decision en un sitio en vez de un injerto repartido.
        // Invariantes del fork, forzadas en CADA arranque y no solo cuando los
        // flags estan a false. La version anterior se saltaba este bloque si
        // firstRunCompleted ya venia a true -- que es justo el caso de quien
        // instala esto sobre una instalacion previa: conservaba los ajustes heredados.
        {
            gamehubnx::AppSettingsData next = settings.get();
            const bool cambia = !next.firstRunCompleted ||
                                !next.catalogDisclaimerAcknowledged ||
                                !next.catalogSourceUrl.empty();
            if (cambia) {
                startupStage("aplicando invariantes de GameHubNX");
                next.firstRunCompleted = true;
                next.catalogDisclaimerAcknowledged = true;
                // Una fuente heredada de otra instalacion haria que la tienda
                // apuntase a otro catalogo; se limpia para que mande el defecto.
                next.catalogSourceUrl.clear();
                std::string error;
                if (!settings.update(next, error))
                    brls::Logger::warning("invariantes: %s", error.c_str());
            }
        }

        startupStage("first main loop");
        bool firstFrame = true;
        uint64_t lastInputMs = now_ms();
        uint64_t lastDeployOfferPollMs = now_ms();
        gamehubnx::SwitchDeployPhase lastDeployPhase =
            gamehubnx::SwitchDeployPhase::Idle;
        bool deployOfferDialogOpen = false;
        // Download/install lifecycle notifications (#33): the main loop is the
        // only place that runs regardless of which screen is focused, so
        // terminal status transitions are surfaced here as toasts instead of
        // requiring the Downloads tab to stay open. The map only tracks tasks
        // observed after startup, so pre-existing rows never re-notify.
        std::unordered_map<std::string, gamehubnx::DownloadStatus> lastTaskStatus;
        uint64_t lastDownloadScanMs = now_ms();
        while (true) {
            const gamehubnx::SwitchDeploySnapshot deployState = deploy.snapshot();
            bool activeTransfer = manager.hasActiveTransfer() ||
                                  deployState.active();
            performance.setActive(activeTransfer);
            metadata.setImageNetwork(
                activeTransfer ? GameMetadataService::ImageNetwork::Throttled
                               : GameMetadataService::ImageNetwork::Full);
            if (!brls::Application::mainLoop())
                break;
            if (deployState.phase != lastDeployPhase) {
                if (deployState.phase == gamehubnx::SwitchDeployPhase::Completed)
                    brls::Application::notify(deployState.detail.empty()
                        ? tr("gamehubnx/deploy/completed")
                        : tr("gamehubnx/deploy/completed_warning",
                             deployState.detail));
                else if (deployState.phase ==
                         gamehubnx::SwitchDeployPhase::Failed)
                    brls::Application::notify(
                        tr("gamehubnx/deploy/failed") +
                        (deployState.detail.empty()
                             ? std::string() : " " + deployState.detail));
                else if (deployState.phase ==
                         gamehubnx::SwitchDeployPhase::Cancelled)
                    brls::Application::notify(tr("gamehubnx/deploy/cancelled"));
                else if (deployState.phase ==
                         gamehubnx::SwitchDeployPhase::Preparing)
                    brls::Application::notify(tr("gamehubnx/deploy/phase_preparing"));
                else if (deployState.phase ==
                         gamehubnx::SwitchDeployPhase::Copying)
                    brls::Application::notify(tr("gamehubnx/deploy/phase_copying"));
                else if (deployState.phase ==
                         gamehubnx::SwitchDeployPhase::Extracting)
                    brls::Application::notify(
                        tr("gamehubnx/deploy/phase_extracting"));
                lastDeployPhase = deployState.phase;
            }

            // Throttled scan of task statuses: notify once per terminal
            // transition (download complete, install complete, download failed).
            const uint64_t frameNowMs = now_ms();
            if (frameNowMs - lastDownloadScanMs >= 1000) {
                lastDownloadScanMs = frameNowMs;
                std::unordered_set<std::string> seen;
                for (const gamehubnx::DownloadTask& task : manager.snapshot()) {
                    seen.insert(task.id);
                    auto previous = lastTaskStatus.find(task.id);
                    if (previous == lastTaskStatus.end()) {
                        lastTaskStatus[task.id] = task.status;
                        continue;
                    }
                    const gamehubnx::DownloadStatus prevStatus = previous->second;
                    if (prevStatus == task.status)
                        continue;
                    lastTaskStatus[task.id] = task.status;
                    if (task.status == gamehubnx::DownloadStatus::Completed)
                        brls::Application::notify(
                            tr("gamehubnx/notify/download_completed", task.name));
                    else if (task.status == gamehubnx::DownloadStatus::Installed)
                        brls::Application::notify(
                            tr("gamehubnx/notify/install_completed", task.name));
                    else if (task.status == gamehubnx::DownloadStatus::Error) {
                        const bool installFailed =
                            prevStatus == gamehubnx::DownloadStatus::Installing ||
                            prevStatus == gamehubnx::DownloadStatus::Committing ||
                            task.mode == gamehubnx::TransferMode::StreamInstall;
                        brls::Application::notify(tr(
                            installFailed ? "gamehubnx/notify/install_failed"
                                          : "gamehubnx/notify/download_failed",
                            task.name));
                    }
                }
                for (auto it = lastTaskStatus.begin();
                     it != lastTaskStatus.end();) {
                    if (seen.count(it->first) == 0)
                        it = lastTaskStatus.erase(it);
                    else
                        ++it;
                }
            }

            const uint64_t deployPollNowMs = now_ms();
            if (deployPollNowMs - lastDeployOfferPollMs >= 10000 &&
                !activeTransfer && !deployState.active() &&
                !deployOfferDialogOpen) {
                lastDeployOfferPollMs = deployPollNowMs;
                deploy.scheduleDeployOfferPoll();
            }
            if (!deployOfferDialogOpen) {
                auto offer = deploy.takePendingDeployOffer();
                if (offer) {
                    deployOfferDialogOpen = true;
                    const std::string offerId = offer->taskId;
                    const auto task = manager.snapshot(offerId);
                    const std::string name =
                        task ? task->name : offerId.substr(0, 8);
                    auto* dialog = new brls::Dialog(
                        tr("gamehubnx/deploy/offer_question", name));
                    dialog->addButton(
                        tr("gamehubnx/deploy/copy"),
                        [&deploy, offerId,
                         inspection = std::move(offer->inspection),
                         &deployOfferDialogOpen]() mutable {
                            deployOfferDialogOpen = false;
                            deploy.dismissDeployOffer(offerId);
                            // Dialog is still alive until this callback
                            // returns. Re-home focus onto the root activity so
                            // pushActivity does not onFocusLost a view that is
                            // about to be deleted with the dialog.
                            const auto stack =
                                brls::Application::getActivitiesStack();
                            if (!stack.empty())
                                brls::Application::giveFocus(
                                    stack.back()->getContentView());
                            if (!inspection.canStart() &&
                                inspection.problem !=
                                    gamehubnx::SwitchDeployProblem::Conflict &&
                                inspection.problem !=
                                    gamehubnx::SwitchDeployProblem::NoSpace &&
                                inspection.problem !=
                                    gamehubnx::SwitchDeployProblem::NoRam) {
                                brls::Application::notify(
                                    inspection.detail.empty()
                                        ? tr("gamehubnx/deploy/failed")
                                        : inspection.detail);
                                return;
                            }
                            brls::Application::pushActivity(
                                new gamehubnx::ui::SwitchDeployPreviewActivity(
                                    std::move(inspection), &deploy),
                                brls::TransitionAnimation::NONE);
                        });
                    dialog->addButton(
                        tr("gamehubnx/common/later"),
                        [&deploy, offerId, &deployOfferDialogOpen] {
                            deployOfferDialogOpen = false;
                            deploy.dismissDeployOffer(offerId);
                        });
                    dialog->open();
                }
            }

            // OLED burn-in guard: after five minutes without a button/touch,
            // cover the UI with a drifting black saver. Any input dismisses it
            // (including D-pad and touch) and resets the idle clock. Open state
            // is derived from the activity stack so a dismiss cannot desync a
            // bool and stack another saver on the next idle period.
            brls::ControllerState pad {};
            std::vector<brls::RawTouchState> touches;
            auto* input = brls::Application::getPlatform()->getInputManager();
            input->updateUnifiedControllerState(&pad);
            input->updateTouchStates(&touches);
            bool touched = false;
            for (const auto& touch : touches) {
                if (touch.pressed) {
                    touched = true;
                    break;
                }
            }
            const bool saverOpen = gamehubnx::ui::burnInSaverIsTop();
            if (gamehubnx::ui::controllerHasButtonDown(pad) || touched) {
                lastInputMs = now_ms();
                if (saverOpen)
                    brls::Application::popActivity(
                        brls::TransitionAnimation::NONE);
            } else if (!saverOpen &&
                       now_ms() - lastInputMs >= gamehubnx::ui::kBurnInIdleMs) {
                brls::Application::pushActivity(
                    new gamehubnx::ui::BurnInSaverActivity(),
                    brls::TransitionAnimation::NONE);
                lastInputMs = now_ms();
            }

            if (firstFrame) {
                startupStage("main loop running");
                deploy.scheduleDeployOfferPoll();
                if (updatePendingConfirmation) {
                    std::string error;
                    if (!launchUpdater.confirmInstalled(error))
                        diagnostic_error("update", "confirm", "error=%s",
                                         error.c_str());
                    else {
                        log_msg("[update] installed update confirmed\n");
                        brls::Application::notify(
                            tr("gamehubnx/settings/updated_to", GAMEHUBNX_VERSION));
                    }
                }
                firstFrame = false;
            }
        }

        startupStage("manager shutdown");
        // The startup title scan references `installed`, which lives on this
        // stack frame — join before anything here is torn down.
        if (installedScanner.thread.joinable())
            installedScanner.thread.join();
        deploy.shutdown();
        // The web server goes first: its threads call into manager, so they
        // must be joined before the manager dies.
        ftpServer.stop();
        updater.shutdown();
        manager.shutdown();
        performance.setActive(false);
    } catch (const std::exception& error) {
        log_msg("[crash] exception at stage '%s': %s\n",
                "see previous startup marker", error.what());
    } catch (...) {
        log_msg("[crash] unknown exception\n");
    }

    startupStage("app-owned teardown complete");
    startupStage("cleanup");
    if (esReady)
        esExit();
    if (nsReady)
        nsExit();
    if (ncmReady)
        ncmExit();
    if (curlReady)
        curl_global_cleanup();
    // Borealis writes into the same handle log_close() owns; drop its pointer
    // before that handle goes away.
    brls::Logger::setLogOutput(nullptr);
    log_close();
    return 0;
}
