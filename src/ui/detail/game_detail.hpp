#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <borealis.hpp>

#include "app/app_settings.hpp"
#include "app/catalog_presentation.hpp"
#include "app/game_update_install.hpp"
#include "app/shop_update_source.hpp"
#include "ui/i18n.hpp"
#include "app/catalog_service.hpp"
#include "app/download_manager.hpp"
#include "app/favorites_service.hpp"
#include "app/game_update_install.hpp"
#include "app/game_metadata_service.hpp"
#include "app/gamehub_catalog.hpp"
#include "app/gamehub_provider.hpp"
#include "app/install_space.hpp"
#include "app/installed_title_service.hpp"
#include "app/magnet_resolver.hpp"
#include "app/mod_index_service.hpp"
#include "app/switch_deploy.hpp"
#include "app/nx_file_types.hpp"
#include "ui/catalog/catalog_helpers.hpp"
#include "ui/common/async_image.hpp"
#include "ui/common/busy_pulse.hpp"
#include "ui/common/install_target_dialog.hpp"
#include "ui/common/storage_meter.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/detail/detail_page.hpp"
#include "ui/detail/screenshot_viewer.hpp"
#include "ui/detail/torrent_selection.hpp"
#include "ui/debrid_ui.hpp"
#include "ui/downloads/details_activity.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui {

// ---------------------------------------------------------------------------
// Install/status button (O5)
// ---------------------------------------------------------------------------
// The primary action button doubles as a live status surface: while a download
// or install is running it keeps its vivid enabled look and dims the
// not-yet-finished remainder (eShop-style progress fill) instead of greying
// out. setProgress(<0) restores the plain button; >=1 leaves it fully filled.
class InstallButton : public brls::Button {
  public:
    void setProgress(float progress) {
        progress_ = progress < 0.0f ? -1.0f : std::min(1.0f, progress);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override {
        // Scrim the pending remainder before the label so the text stays crisp.
        if (progress_ >= 0.0f && progress_ < 1.0f) {
            float radius   = getCornerRadius();
            float doneWidth = width * progress_;
            nvgSave(vg);
            nvgIntersectScissor(vg, x + doneWidth, y, width - doneWidth, height);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, width, height, radius);
            nvgFillColor(vg, theme::panel());
            nvgFill(vg);
            nvgRestore(vg);
        }
        brls::Button::draw(vg, x, y, width, height, style, ctx);
    }

  private:
    float progress_ = -1.0f;
};

// ---------------------------------------------------------------------------
// Full-screen game page (eShop-style detail + one-tap install)
// ---------------------------------------------------------------------------

class GameDetailActivity : public brls::Activity {
public:
    // hashLower + failure ("" clears) recorded back into the catalog; onChange
    // asks the catalog list to re-badge.
    using FailureCallback =
        std::function<void(const std::string&, const std::string&)>;
    using ChangeCallback = std::function<void()>;
    // O12: fired (deferred one frame) when the page is torn down after the
    // B-pop, so the catalog can re-seat scroll + focus on the opening card.
    using CloseCallback = std::function<void()>;
    /* Abrir la pantalla de DLC y actualizaciones. La pone quien crea la ficha
       --la vista de catalogo-- porque es quien conoce las dos clases: esta
       cabecera no puede incluir shop_addons.hpp, que la incluye a ella. */
    using AddOnsCallback = std::function<void(const CatalogEntry& base,
                                             const std::vector<CatalogEntry>&)>;

    GameDetailActivity(CatalogEntry entry, std::string lastFailure,
                       DownloadManager* manager, GameMetadataService* metadata,
                       InstalledTitleService* installed, AppSettings* settings,
                       ModIndexService* mods,
                        FailureCallback onFailure, ChangeCallback onChange,
                        CloseCallback onClose = nullptr,
                        FavoritesService* favorites = nullptr,
                        SwitchDeployService* deploy = nullptr,
                        std::vector<CatalogEntry> addOns = {},
                        bool catalogKnowsAddOns = false,
                        AddOnsCallback onAddOns = nullptr)
        : addOns_(std::move(addOns)),
          catalogKnowsAddOns_(catalogKnowsAddOns),
          onAddOns_(std::move(onAddOns)), entry_(std::move(entry)),
          lastFailure_(std::move(lastFailure)),
          manager_(manager), metadata_(metadata), installed_(installed),
          settings_(settings), mods_(mods), favorites_(favorites),
          deploy_(deploy),
          onFailure_(std::move(onFailure)), onChange_(std::move(onChange)),
          onClose_(std::move(onClose)),
          alive_(std::make_shared<std::atomic<bool>>(true)),
          cancelled_(std::make_shared<std::atomic<bool>>(false)) {
        const GameMetadata* found = metadata_->findByInfoHash(entry_.infoHash);
        presentation_ = resolveCatalogPresentation(entry_, found,
                                                   catalogTextPreference());
        titleId_ = presentation_.titleId;
        playersFact_ = playersFact(found);

        // F3: eShop-style two-column page. Left column is fixed (cover +
        // install button + size/status); the right column scrolls on its own.
        auto* content = new brls::Box(brls::Axis::ROW);
        content->setPadding(24, 40, 24, 40);
        buildLeftColumn(content);
        buildRightColumn(content);
        installBytes_ = entry_.size;
        refreshSizeMeter();

        frame_ = new brls::AppletFrame(content);
        frame_->setTitle(presentation_.title);
    }

    ~GameDetailActivity() override {
        alive_->store(false);
        cancelled_->store(true);
        timer_.stop();
        stopBusyPulse(primary_);
        stopBusyPulse(statusLabel_);
        if (onChange_)
            onChange_();  // refresh the row badge on the way back
        // O12: deferred a frame — the activity is mid-teardown here and
        // popActivity() has already unwound the focus stack, so the catalog
        // can safely take the focus back now.
        if (onClose_)
            brls::sync([onClose = std::move(onClose_)] { onClose(); });
    }

    brls::View* createContentView() override { return frame_; }

    void onContentAvailable() override {
        registerAction(tr("gamehubnx/common/cancel"), brls::BUTTON_Y,
                       [this](brls::View*) {
            if (busy_)
                cancelled_->store(true);
            return true;
        });
        // Same hotkey as the catalog grid, and the hint is what names the
        // square ★ button above — this page has room for it where the catalog
        // does not.
        if (favorites_) {
            registerAction(tr("gamehubnx/catalog/action_favorite"),
                           brls::BUTTON_RT, [this](brls::View*) {
                onFavorite();
                return true;
            });
        }
        refreshButtons();
        timer_.setCallback([this] {
            refreshButtons();
            // nsGetStorageSize is a real syscall — re-read it every ~2s, not on
            // every 500ms button tick. Same cadence as the sidebar footer.
            if (++storageTick_ >= 4) {
                storageTick_ = 0;
                refreshSizeMeter();
            }
        });
        timer_.start(500);
        brls::Button* focus = primary_;
        if (focus)
            brls::Application::giveFocus(focus);
    }

private:
    // Las medidas viven en detail_page para que las dos fichas no puedan
    // discrepar. El comentario de por que son estas y no otras esta alli.
    static constexpr float kLeftColumnWidth = detail_page::kLeftColumnWidth;
    // coverUrl is the metadata banner for nearly every real entry, and an
    // eShop banner is 16:9 — at 320 wide it already draws 180 tall under FIT,
    // so anything past 200 was letterbox the column could not afford.
    static constexpr float kCoverHeight = detail_page::kCoverHeight;

    // Left column: cover, full-width Install / Select files, size, status (F3).
    //
    // The column does not scroll, so its height is a hard budget: 720 minus the
    // AppletFrame header (88) and footer (73) minus this page's 24px top/bottom
    // padding leaves 511px. Everything below is sized to fit inside it with
    // room to spare — overflow here lands on top of the storage meter and the
    // bottom bar, because Yoga runs with web defaults and nothing clips it.
    void buildLeftColumn(brls::Box* content) {
        auto* left = new brls::Box(brls::Axis::COLUMN);
        left->setWidth(kLeftColumnWidth);
        left->setMarginRight(32);
        // Backstop for the budget above: statusLabel_ carries torrent errors and
        // resolve progress, both of which can wrap to an unpredictable number of
        // lines. Same guarantee StorageMeter gives itself.
        left->setClipsToBounds(true);

        if (!presentation_.coverUrl.empty()) {
            // The plate doubles as the placeholder tile until the art arrives
            // (and stays visible as the letterbox behind a 16:9 banner), the
            // same way the catalog card's cover box works.
            // El plate tomaba SIEMPRE los 320 de la columna, que es 16:9 a
            // esta altura. Con una portada 2:3 --la de Switch en GameHub-- eso
            // dejaba dos bandas de fondo a los lados y la caja se veia
            // encogida. Ahora el ancho sale de la proporcion en vigor y el
            // plate se centra; la altura no se toca, que es el presupuesto de
            // la columna documentado arriba.
            //
            // Pero SOLO cuando lo que se pinta es la portada. Si la entrada
            // cruza con el indice de metadatos, coverUrl es su banner de eShop
            // --16:9-- y estrecharlo a 2:3 lo aplastaria: ahi el plate sigue
            // ocupando la columna entera, como siempre. La condicion es la
            // misma que usa catalog_presentation para decidirlo:
            // iconPreserveAspect marca "esto es un poster", y coverUrl igual a
            // iconUrl significa que no hubo banner que preferir.
            const bool esPortada =
                presentation_.iconPreserveAspect &&
                presentation_.coverUrl == presentation_.iconUrl;
            const float coverWidth = esPortada
                ? std::min(kLeftColumnWidth, kCoverHeight * coverAspect())
                : kLeftColumnWidth;
            auto* plate = new brls::Box();
            plate->setWidth(coverWidth);
            plate->setHeight(kCoverHeight);
            plate->setMarginLeft((kLeftColumnWidth - coverWidth) / 2.0f);
            plate->setCornerRadius(theme::kRadiusLarge);
            plate->setBackgroundColor(theme::surface());
            auto* cover = new AsyncRgbaImage();
            cover->setWidth(coverWidth);
            cover->setHeight(kCoverHeight);
            cover->setPositionType(brls::PositionType::ABSOLUTE);
            cover->setPositionTop(0);
            cover->setPositionLeft(0);
            cover->setCornerRadius(theme::kRadiusLarge);
            cover->setScalingType(brls::ImageScalingType::FIT);
            // FIT letterboxes; with clip on, borealis fills the whole view
            // rect with the pattern and the margin samples clamped edge texels
            // (stretched bands). Clip off draws only the fitted image rect.
            cover->setClipsToBounds(false);
            loadImageInto(cover, metadata_, presentation_.coverUrl);
            plate->addView(cover);
            left->addView(plate);
        }

        primary_ = new InstallButton();
        primary_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
        primary_->setFontSize(theme::kFontBody);
        primary_->setHeight(64);
        primary_->setMarginTop(16);
        primary_->setText(tr("gamehubnx/common/install"));
        primary_->registerClickAction([this](brls::View*) {
            onPrimary();
            return true;
        });
        left->addView(primary_);

        // Boton de complementos. Solo aparece si este juego tiene alguno en la
        // tienda: antes la ficha decia cuantos habia y no daba manera de
        // llegar a ellos, que era la queja.
        if (!addOns_.empty() && onAddOns_) {
            addOns_button_ = new brls::Button();
            addOns_button_->setStyle(&brls::BUTTONSTYLE_DEFAULT);
            addOns_button_->setFontSize(theme::kFontBody);
            addOns_button_->setHeight(52);
            addOns_button_->setMarginTop(10);
            addOns_button_->setText(tr("gamehubnx/detail/addons_button",
                                       (int)addOns_.size()));
            addOns_button_->registerClickAction([this](brls::View*) {
                if (onAddOns_)
                    onAddOns_(entry_, addOns_);
                return true;
            });
            left->addView(addOns_button_);
        }

        installContract_ = new brls::Label();
        installContract_->setFontSize(theme::kFontCaption);
        installContract_->setTextColor(theme::textTertiary());
        installContract_->setMarginTop(6);
        installContract_->setSingleLine(false);
        installContract_->setText(tr("gamehubnx/detail/smart_install_contract"));
        left->addView(installContract_);

        // File selection and the wishlist toggle share one row: a fourth full-width
        // button does not fit the column budget, and a square star needs no
        // translation (Russian "В избранном" would not fit it anyway).
        auto* actions = new brls::Box(brls::Axis::ROW);
        actions->setMarginTop(12);

        secondary_ = new brls::Button();
        secondary_->setStyle(&brls::BUTTONSTYLE_DEFAULT);
        secondary_->setFontSize(theme::kFontSmall);
        secondary_->setGrow(1);
        secondary_->setHeight(56);
        secondary_->setText(tr("gamehubnx/common/choose_files"));
        secondary_->registerClickAction([this](brls::View*) {
            onSecondary();
            return true;
        });
        actions->addView(secondary_);

        // Wishlist toggle: the same star the catalog grid puts on the card,
        // reachable without hunting for the ZR hotkey.
        if (favorites_) {
            favorite_ = new brls::Button();
            favorite_->setStyle(&brls::BUTTONSTYLE_DEFAULT);
            favorite_->setFontSize(theme::kFontHeading);
            favorite_->setWidth(56);
            favorite_->setHeight(56);
            favorite_->setMarginLeft(8);
            favorite_->setText("★");
            favorite_->registerClickAction([this](brls::View*) {
                onFavorite();
                return true;
            });
            actions->addView(favorite_);
        }
        left->addView(actions);

        // How much of the card this release eats. Seeded from the catalog size
        // and refined to the exact figure once the torrent metadata resolves.
        sizeMeter_ = new StorageMeter();
        sizeMeter_->setHeader(storageMeterHeader(
            settings_ ? installTargetFor(settings_->get().installLocation)
                      : manager_->installTarget()));
        sizeMeter_->setMarginTop(20);
        left->addView(sizeMeter_);

        statusLabel_ = new brls::Label();
        statusLabel_->setFontSize(theme::kFontCaption);
        statusLabel_->setMarginTop(8);
        statusLabel_->setTextColor(theme::accent());
        statusLabel_->setText(tr("gamehubnx/detail/install_hint"));
        left->addView(statusLabel_);

        content->addView(left);
    }

    // Right column (scrolls): fact table, screenshots, description. The game
    // title already lives in the AppletFrame header, so do not repeat it here.
    void buildRightColumn(brls::Box* content) {
        auto* right = new brls::Box(brls::Axis::COLUMN);
        right->setPadding(0, 12, 24, 0);

        buildFactsTable(right);

        // El carril lo pinta detail_page, igual que en la ficha de ROM: mismas
        // medidas, mismo trato de las bandas del ajuste y misma ruta de foco
        // hacia el boton principal.
        detail_page::addScreenshotRail(right, metadata_,
                                       presentation_.screenshots,
                                       presentation_.title, primary_);

        buildDescription(right);

        std::string warn;
        if (!lastFailure_.empty()) {
            warn = tr("gamehubnx/detail/last_attempt", lastFailure_);
        } else {
            std::string health = badgeForCatalogHealth(entry_);
            if (!health.empty() && health != tr("gamehubnx/health/fresh")) {
                warn = tr("gamehubnx/detail/catalog_health", health);
                if (!entry_.healthReason.empty())
                    warn = tr("gamehubnx/detail/catalog_health_reason", health,
                              entry_.healthReason);
            }
        }
        if (!warn.empty()) {
            auto* warning = new brls::Label();
            warning->setFontSize(theme::kFontCaption);
            warning->setMarginTop(16);
            warning->setTextColor(theme::warning());
            warning->setText(warn);
            right->addView(warning);
        }

        // Raw catalog release title (moved off the list per F2).
        auto* release = new brls::Label();
        release->setFontSize(theme::kFontCaption);
        release->setMarginTop(16);
        release->setTextColor(theme::textTertiary());
        release->setText(tr("gamehubnx/detail/release_line", entry_.title));
        right->addView(release);

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1);
        scroll->setContentView(right);
        content->addView(scroll);
    }

    // S4: facts as label/value rows instead of one glued string.
    void buildFactsTable(brls::Box* right) {
        auto* table = new brls::Box(brls::Axis::COLUMN);
        table->setMarginTop(8);
        addFactRow(table, tr("gamehubnx/detail/fact_install_state"),
                   installed_ && installed_->contains(titleId_)
                       ? tr("gamehubnx/detail/install_state_installed")
                       : tr("gamehubnx/detail/install_state_not_installed"));
        addFactRow(table, tr("gamehubnx/detail/fact_installed_version"),
                   factVersion(installedVersionForTitle()));
        addFactRow(table, tr("gamehubnx/detail/fact_available_version"),
                   factVersion(latestVersionForEntry()));
        addFactRow(table, tr("gamehubnx/detail/fact_developer"),
                   presentation_.developer);
        addFactRow(table, tr("gamehubnx/detail/fact_publisher"),
                   presentation_.publisher);
        addFactRow(table, tr("gamehubnx/detail/fact_release"),
                   presentation_.releaseDate);
        addFactRow(table, tr("gamehubnx/detail/fact_genre"), presentation_.genre);
        // Region e idiomas solo llegan de la tienda GameHub; addFactRow ya se
        // salta las filas vacias, asi que para cualquier otro catalogo la tabla
        // sale igual que antes.
        addFactRow(table, tr("gamehubnx/detail/fact_region"), presentation_.region);
        addFactRow(table, tr("gamehubnx/detail/fact_languages"), presentation_.languages);
        addFactRow(table, tr("gamehubnx/detail/fact_players"), playersFact_);
        addFactRow(table, tr("gamehubnx/detail/fact_multiplayer"),
                   presentation_.multiplayer);
        if (preferCatalogNativeText()) {
            addFactRow(table, tr("gamehubnx/detail/fact_interface_lang"),
                       entry_.interfaceLang);
            addFactRow(table, tr("gamehubnx/detail/fact_voice_lang"),
                       entry_.voiceLang);
        }
        addFactRow(table, tr("gamehubnx/detail/fact_performance"),
                   presentation_.performance);
        addFactRow(table, tr("gamehubnx/detail/fact_size"),
                   entry_.size ? formatBytes(entry_.size)
                               : tr("gamehubnx/common/unknown"));
        addFactRow(table, tr("gamehubnx/detail/fact_title_id"), titleId_);
        addFactRow(table, tr("gamehubnx/detail/fact_dlc"), dlcFact());
        addFactRow(table, tr("gamehubnx/detail/fact_mods"), modsFact());
        right->addView(table);
    }

    size_t dlcCount() const {
        size_t n = 0;
        for (const CatalogEntry& e : addOns_)
            if (gamehub::isDlcTitleId(e.titleId))
                ++n;
        return n;
    }

    /// Version para la tabla de datos.
    ///
    /// Un titulo sin parche reporta version 0, que formatTitleVersion convierte
    /// en "0.0.0". Eso no le dice nada a nadie: significa el juego tal y como
    /// salio, sin actualizar. Se dice con palabras.
    static std::string factVersion(const std::string& decimal) {
        const std::string text = formatTitleVersion(decimal);
        return text == "0.0.0" ? tr("gamehubnx/detail/version_base") : text;
    }

    std::string dlcFact() const {
        // Dos datos distintos que antes se confundian en uno: lo que hay en la
        // tienda y lo que ya esta puesto en la consola. Antes solo se contaba
        // lo instalado, asi que un juego con veinte DLC disponibles no lo
        // decia por ningun lado -- y esos veinte aparecian sueltos en la
        // rejilla, que era la queja.
        // addOns_ trae DLC y actualizaciones; esta fila cuenta DLC. La
        // actualizacion ya tiene la suya ("version disponible") y sumarla aqui
        // inflaba la cifra con algo que el usuario ya estaba viendo.
        const size_t disponibles = dlcCount();
        const bool puesto = installed_ && !titleId_.empty() &&
                            installed_->contains(titleId_);
        const int instalados =
            puesto ? installed_->dlcCountForBase(titleId_) : 0;

        if (disponibles == 0) {
            if (puesto)
                return tr("gamehubnx/detail/dlc_installed", instalados);
            // Con el catalogo completo delante, cero DLC es un dato: se dice.
            // Sin el, callarse es lo honesto -- la fila desaparece, que es como
            // se comportaba antes de saber distinguir los dos casos.
            return catalogKnowsAddOns_ ? tr("gamehubnx/detail/dlc_none")
                                       : std::string();
        }
        if (!puesto)
            return tr("gamehubnx/detail/dlc_available", (int)disponibles);
        return tr("gamehubnx/detail/dlc_available_installed",
                  (int)disponibles, instalados);
    }

    // ModCD carries mods for this title id (in-memory lookup — the table is
    // fetched with the catalogue, never from this page). Empty = no row.
    std::string modsFact() const {
        if (!mods_ || titleId_.empty() || !mods_->has(titleId_))
            return std::string();
        const uint32_t count = mods_->modCount(titleId_);
        if (count == 0)
            return tr("gamehubnx/detail/mods_available");
        return tr("gamehubnx/detail/mods_count", count);
    }

    // La fila la pinta detail_page, compartida con la ficha de ROM. Tener dos
    // copias fue lo que dejo las dos pantallas con distinto aspecto.
    void addFactRow(brls::Box* table, const std::string& name,
                    const std::string& value) {
        detail_page::addFactRow(table, name, value);
    }

    // S5: reversible "Show more" instead of a hard cut.
    void buildDescription(brls::Box* right) {
        std::string text = presentation_.description;
        if (text.empty()) {
            auto* missing = new brls::Label();
            missing->setFontSize(theme::kFontSmall);
            missing->setMarginTop(24);
            missing->setText(tr("gamehubnx/detail/no_description"));
            right->addView(missing);
            return;
        }
        auto* desc = new brls::Label();
        desc->setFontSize(theme::kFontSmall);
        desc->setMarginTop(24);
        bool truncated = text.size() > 900;
        desc->setText(truncated ? shortDescription(text) : text);
        right->addView(desc);
        if (truncated) {
            auto* more = new brls::Button();
            more->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
            more->setFontSize(theme::kFontSmall);
            more->setTextColor(theme::accent());
            more->setMarginTop(4);
            more->setText(tr("gamehubnx/detail/show_more"));
            more->registerClickAction([this, desc, more,
                                       text = std::move(text)](brls::View*) {
                desc->setText(text);
                more->setVisibility(brls::Visibility::GONE);
                if (primary_)
                    brls::Application::giveFocus(primary_);
                return true;
            });
            right->addView(more);
        }
    }

    // Find the managed task for this game, if any.
    const DownloadTask* currentTask() {
        cache_ = manager_->snapshot();
        std::string hash = catalogLower(entry_.infoHash);
        for (const DownloadTask& task : cache_)
            if (catalogLower(task.id) == hash)
                return &task;
        return nullptr;
    }

    static std::string installButtonLabel(const DownloadTask& task) {
        switch (task.status) {
            case DownloadStatus::Queued:
                return tr("gamehubnx/detail/status_queued");
            case DownloadStatus::Fetching:
                return tr("gamehubnx/detail/status_fetching",
                          percentOf(static_cast<float>(task.fetchProgress)));
            case DownloadStatus::Checking:
                return tr("gamehubnx/downloads/status_checking");
            case DownloadStatus::Downloading: {
                return tr("gamehubnx/detail/status_downloading",
                          percentOf(progressOf(task)));
            }
            case DownloadStatus::Installing:
            case DownloadStatus::Committing: {
                return tr("gamehubnx/detail/status_installing",
                          percentOf(installProgressOf(task)));
            }
            case DownloadStatus::Verifying:
                return tr("gamehubnx/detail/status_verifying",
                          percentOf(progressOf(task)));
            case DownloadStatus::Paused: {
                int pct = percentOf(progressOf(task));
                return pct > 0
                    ? tr("gamehubnx/detail/status_paused_percent", pct)
                    : tr("gamehubnx/detail/status_paused");
            }
            case DownloadStatus::Completed:
                return tr("gamehubnx/detail/status_downloaded");
            case DownloadStatus::Installed:
                return tr("gamehubnx/detail/status_installed");
            case DownloadStatus::Error: {
                int pct = percentOf(progressOf(task));
                return pct > 0
                    ? tr("gamehubnx/detail/status_error_percent", pct)
                    : tr("gamehubnx/detail/status_error");
            }
            case DownloadStatus::Removing:
                return tr("gamehubnx/detail/status_removing");
        }
        return tr("gamehubnx/common/install");
    }

    // Fill fraction the status button paints while a task is live: install
    // phases track installed bytes, everything else tracks downloaded bytes.
    static float progressForButton(const DownloadTask& task) {
        switch (task.status) {
            case DownloadStatus::Fetching:
                return std::clamp(static_cast<float>(task.fetchProgress),
                                  0.0f, 1.0f);
            case DownloadStatus::Installing:
            case DownloadStatus::Committing:
                return installProgressOf(task);
            case DownloadStatus::Completed:
            case DownloadStatus::Installed:
                return 1.0f;
            case DownloadStatus::Queued:
                return 0.0f;
            default:
                return progressOf(task);
        }
    }

    // Repaint the install-size bar against the current free space. installBytes_
    // starts as the catalog-declared size and becomes exact once the torrent
    // metadata has been read (see finishImport).
    void refreshSizeMeter() {
        if (!sizeMeter_)
            return;
        const auto target = settings_
            ? installTargetFor(settings_->get().installLocation)
            : manager_->installTarget();
        sizeMeter_->setHeader(storageMeterHeader(target));
        const gamehubnx::StorageSpaceSnapshot storage =
            gamehubnx::queryInstallStorageSpace(target, manager_->rootPath());
        if (!storage.available) {
            sizeMeter_->setUnavailable();
            return;
        }
        sizeMeter_->setGameEstimate(storage.totalBytes, storage.freeBytes,
                                    installBytes_,
                                    installBytes_ > storage.freeBytes,
                                    sizeExact_);
    }

    // Pulse Install + status while magnet/debrid resolve is in flight (#19).
    void setBusy(bool busy) {
        busy_ = busy;
        if (busy) {
            startBusyPulse(primary_);
            startBusyPulse(statusLabel_);
        } else {
            stopBusyPulse(primary_);
            stopBusyPulse(statusLabel_);
        }
    }

    // Star/unstar this game. The cap is reported in the user's words; any
    // other failure is a write error worth showing verbatim.
    void onFavorite() {
        if (!favorites_)
            return;
        const bool starred = favorites_->contains(entry_.infoHash);
        if (!starred &&
            favorites_->items().size() >= FavoritesService::kMaxFavorites) {
            brls::Application::notify(
                tr("gamehubnx/catalog/favorites_full",
                   FavoritesService::kMaxFavorites));
            return;
        }
        std::string error;
        favorites_->toggle(entry_.infoHash, presentation_.title, error);
        if (!error.empty()) {
            brls::Application::notify(
                tr("gamehubnx/catalog/favorites_failed", error));
            return;
        }
        refreshFavoriteButton();
        // The catalog rebuilds its grid (and the star badge) on this.
        if (onChange_)
            onChange_();
    }

    // The glyph never changes \u2014 an outline star (U+2606) is not exercised by
    // any golden baseline, so there is no evidence the console shared font
    // carries it. State rides on the fill instead, the same way the catalog
    // header's \u2605 filter chip reads as on/off.
    void refreshFavoriteButton() {
        if (!favorite_)
            return;
        const bool starred = favorites_ &&
                             favorites_->contains(entry_.infoHash);
        favorite_->setStyle(starred ? &brls::BUTTONSTYLE_PRIMARY
                                    : &brls::BUTTONSTYLE_DEFAULT);
    }

    // Reflect live task state on the buttons. Skipped while resolving so the
    // inline progress text isn't clobbered.
    void refreshButtons() {
        refreshFavoriteButton();
        if (busy_)
            return;
        const DownloadTask* task = currentTask();
        if (task) {
            operationMessage_.clear();
            // O5: the button is the status surface — it stays vivid (enabled)
            // and shows a progress fill instead of greying out. Paused/Error
            // become an actionable "Resume".
            bool actionable = task->status == DownloadStatus::Paused ||
                              task->status == DownloadStatus::Error;
            if (actionable) {
                setTextIfChanged(primary_, tr("gamehubnx/common/resume"));
                primary_->setProgress(-1.0f);
            } else {
                setTextIfChanged(primary_, installButtonLabel(*task));
                primary_->setProgress(progressForButton(*task));
            }
            primary_->setState(brls::ButtonState::ENABLED);
            setTextIfChanged(secondary_, tr("gamehubnx/detail/view_download"));
            secondary_->setState(brls::ButtonState::ENABLED);
            if (installContract_)
                installContract_->setVisibility(brls::Visibility::GONE);
            if (task->status == DownloadStatus::Error && !task->error.empty())
                setTextIfChanged(statusLabel_, task->error);
        } else {
            setTextIfChanged(primary_, tr("gamehubnx/common/install"));
            primary_->setProgress(-1.0f);
            primary_->setState(brls::ButtonState::ENABLED);
            setTextIfChanged(secondary_, tr("gamehubnx/detail/install_options"));
            secondary_->setState(brls::ButtonState::ENABLED);
            if (installContract_)
                installContract_->setVisibility(brls::Visibility::VISIBLE);
            if (!operationMessage_.empty())
                setTextIfChanged(statusLabel_, operationMessage_);
            else if (installed_ && installed_->contains(titleId_)) {
                const std::string latestXyz =
                    formatTitleVersion(latestVersionForEntry());
                if (!latestXyz.empty() &&
                    titleVersionIsNewer(latestVersionForEntry(),
                                        installedVersionForTitle()))
                    setTextIfChanged(
                        statusLabel_,
                        tr("gamehubnx/detail/update_available_hint", latestXyz));
                else
                    setTextIfChanged(statusLabel_,
                                     tr("gamehubnx/detail/installed_hint"));
            } else
                setTextIfChanged(statusLabel_,
                                 tr("gamehubnx/detail/install_hint"));
        }
    }

    void onPrimary() {
        if (busy_)
            return;
        const DownloadTask* task = currentTask();
        if (task) {
            if (task->status == DownloadStatus::Paused ||
                task->status == DownloadStatus::Error)
                manager_->resume(task->id);
            else
                // O5: tapping the live status button opens the download details.
                brls::Application::pushActivity(
                    new DetailsActivity(task->id, manager_, deploy_));
            return;
        }
        // One-tap install: resolve, then queue silently (picker only on Select files).
        startInstall(false);
    }

    void onSecondary() {
        if (busy_)
            return;
        const DownloadTask* task = currentTask();
        if (task) {
            brls::Application::pushActivity(
                new DetailsActivity(task->id, manager_, deploy_));
            return;
        }
        // Install options: always open the per-file picker after resolve.
        startInstall(true);
    }

    // One-tap: resolve the magnet inline, then import immediately (no second
    // dialog) unless forcePicker is set (the "Select files" path), which always
    // opens the per-file selection screen after resolve.
    void startInstall(bool forcePicker) {
        if (busy_)
            return;
        // La tienda va PRIMERO porque ninguna de las dos ramas de abajo puede
        // servirla: no hay magnet que resolver --entry_.magnetUri viene vacio,
        // el indice solo trae directUrl-- ni cuenta de debrid que vincular. Es
        // una descarga cliente-servidor de toda la vida contra el servidor del
        // propio usuario.
        if (entry_.isDirectDownload()) {
            startShopInstall(TransferMode::StreamInstall, forcePicker);
            return;
        }
        if (debridModeActive(settings_)) {
            startDebridInstall(TransferMode::StreamInstall, forcePicker);
            return;
        }
        setBusy(true);
        operationMessage_.clear();
        cancelled_->store(false);
        primary_->setState(brls::ButtonState::DISABLED);
        secondary_->setState(brls::ButtonState::DISABLED);
        primary_->setText(tr("gamehubnx/detail/resolving"));
        statusLabel_->setText(tr("gamehubnx/detail/finding_peers") +
                              tr("gamehubnx/detail/cancel_hint"));

        auto alive = alive_;
        auto cancelled = cancelled_;
        uint32_t serial = gCatalogTempSerial.fetch_add(1);
        std::string tmp = manager_->rootPath() + "/_catalog_tmp_" +
                          catalogLower(entry_.infoHash) + "_" +
                          std::to_string(serial) + ".torrent";
        std::string magnet = entry_.magnetUri;
        std::vector<uint8_t> infoDict = entry_.infoDict;
        std::string telemetryTag = catalogLower(entry_.infoHash);
        uint64_t startedMs = now_ms();
        brls::async([this, alive, cancelled, magnet, infoDict, tmp,
                     forcePicker, telemetryTag, startedMs] {
            std::string err;
            MagnetResolver resolver;
            auto progress = [this, alive, last = std::string()](
                                const gamehubnx::MagnetProgress& p) mutable {
                std::string text;
                switch (p.stage) {
                    case gamehubnx::MagnetProgress::Stage::FindingPeers:
                        text = tr("gamehubnx/detail/finding_peers");
                        break;
                    case gamehubnx::MagnetProgress::Stage::Connecting:
                        text = tr("gamehubnx/detail/contacting_peer",
                                  p.peerIndex, p.peerCount);
                        break;
                    case gamehubnx::MagnetProgress::Stage::FetchingMetadata:
                        text = tr("gamehubnx/detail/fetching_metadata",
                                  p.completedPieces, p.totalPieces);
                        break;
                    case gamehubnx::MagnetProgress::Stage::Validating:
                        text = tr("gamehubnx/detail/validating");
                        break;
                }
                if (text == last)
                    return;
                last = text;
                brls::sync([this, alive, text] {
                    if (alive->load() && busy_)
                        statusLabel_->setText(text + tr("gamehubnx/detail/cancel_hint"));
                });
            };
            std::vector<uint8_t> initialPeers;
            bool ok = resolver.resolveToFile(
                magnet, tmp, *cancelled, progress, err, &initialPeers,
                infoDict.empty() ? nullptr : &infoDict);
            telemetry_log("magnet", telemetryTag.c_str(),
                          "event=resolve ok=%d cancelled=%d duration_ms=%llu "
                          "verified_peers=%u",
                          ok ? 1 : 0, cancelled->load() ? 1 : 0,
                          (unsigned long long)(now_ms() - startedMs),
                          static_cast<unsigned>(initialPeers.size() / 6));
            brls::sync([this, alive, ok, err, tmp, forcePicker,
                        initialPeers = std::move(initialPeers)]() mutable {
                if (!alive->load()) {
                    ::unlink(tmp.c_str());
                    return;
                }
                setBusy(false);
                std::string hash = catalogLower(entry_.infoHash);
                if (!ok) {
                    std::string reason = classifyResolveFailure(err);
                    if (onFailure_)
                        onFailure_(hash, reason);
                    diagnostic_error("magnet", hash.c_str(), "error=%s",
                                     err.c_str());
                    operationMessage_ = reason;
                    refreshButtons();
                    brls::Application::notify(err);
                    ::unlink(tmp.c_str());
                    return;
                }
                if (onFailure_)
                    onFailure_(hash, "");  // clear stale failure
                finishImport(tmp, forcePicker, std::move(initialPeers));
            });
        });
    }

    // Instalar una entrada de la tienda GameHub.
    //
    // GameHubProvider implementa la interfaz de debrid, pero de debrid no tiene
    // nada: el "handle" ES la URL, no crea nada remoto y no necesita clave. Se
    // instancia aqui a mano en vez de por makeDebridProvider() a proposito --
    // esa fabrica devuelve TorBox/Real-Debrid/TorrServer segun un ajuste que en
    // este fork ya no existe, y por ahi se pedia vincular una cuenta ajena para
    // bajar un fichero del servidor de casa.
    void startShopInstall(TransferMode mode, bool forcePicker) {
        auto alive = alive_;
        askInstallTarget(manager_->rootPath(),
                         [this, alive, mode,
                          forcePicker](install::InstallStorageTarget target) {
            if (!alive->load())
                return;
            installTarget_ = target;
            beginShopInstall(mode, forcePicker);
        });
    }

    void beginShopInstall(TransferMode mode, bool forcePicker) {
        setBusy(true);
        operationMessage_.clear();
        cancelled_->store(false);
        primary_->setState(brls::ButtonState::DISABLED);
        secondary_->setState(brls::ButtonState::DISABLED);
        primary_->setText(tr("gamehubnx/detail/resolving"));
        statusLabel_->setText(tr("gamehubnx/detail/shop_preparing"));

        auto alive = alive_;
        auto cancelled = cancelled_;
        const CatalogEntry entry = entry_;
        brls::async([this, alive, cancelled, entry, mode, forcePicker] {
            // createFromMagnet solo valida que la URL sea del host configurado;
            // fetchInfo le pregunta el tamano al servidor con un HEAD. Ninguna
            // de las dos deja nada que limpiar si esto se cancela.
            GameHubProvider provider;
            std::string id;
            std::string error;
            DebridInfo info;
            const bool ok =
                provider.createFromMagnet(entry.directUrl, id, error) &&
                provider.fetchInfo(id, info, error);
            brls::sync([this, alive, cancelled, ok, error, id, info, entry,
                        mode, forcePicker] {
                if (!alive->load())
                    return;
                setBusy(false);
                if (cancelled->load()) {
                    refreshButtons();
                    return;
                }
                if (!ok || info.files.empty()) {
                    operationMessage_ = error.empty()
                        ? tr("gamehubnx/detail/shop_unavailable") : error;
                    refreshButtons();
                    brls::Application::notify(operationMessage_);
                    return;
                }
                queueShopImport(entry, id, info, mode, forcePicker);
            });
        });
    }

    // Una entrada de la tienda es UN fichero, asi que aqui no hay nada que
    // elegir ni que adivinar: se marca para instalar y se encola. No se pasa
    // por smartInstallMask() --que existe para decidir que ficheros de un
    // torrent con muchos hacen falta-- porque con uno solo su respuesta util es
    // siempre la misma, y su respuesta inutil (0 seleccionados, "abre las
    // opciones") dejaria colgado justo lo que el usuario acaba de pulsar.
    void queueShopImport(const CatalogEntry& entry, const std::string& id,
                         const DebridInfo& info, TransferMode mode,
                         bool forcePicker) {
        const DebridFile& file = info.files.front();
        // La tienda sirve .nsp y .nsz, que se instalan. Cualquier otra cosa se
        // baja y se deja en la SD: mejor un fichero puesto donde el usuario
        // pueda verlo que un error por no saber instalarlo.
        const bool package = isPackageName(file.path);

        DebridImport import;
        import.infoHash = catalogLower(entry.infoHash);
        import.name = info.name.empty() ? entry.title : info.name;
        import.totalBytes = info.bytes ? info.bytes : entry.size;
        // `provider` se queda en su valor por defecto: DownloadManager lo
        // ignora y siempre construye GameHubProvider (ver makeProvider).
        import.debridId = id;
        import.mode = package ? mode : TransferMode::DownloadOnly;
        import.fileSelection = {static_cast<uint8_t>(
            package ? FileAction::Install : FileAction::Download)};
        import.packageCount = package ? 1u : 0u;
        import.hasInstallTarget = true;
        import.installTarget = installTarget_;

        if (forcePicker) {
            TorrentPreview preview;
            preview.name = import.name;
            preview.totalBytes = import.totalBytes;
            preview.fileCount = 1;
            preview.files.push_back({file.path, file.bytes, package,
                                     isCompressedName(file.path),
                                     isCartridgeName(file.path)});
            preview.packageCount = package ? 1u : 0u;
            preview.cartridgeCount = isCartridgeName(file.path) ? 1u : 0u;
            // Sin callback de abandono: no se creo nada en ningun servidor.
            // Y con rotulo propio: esta pantalla nacio para elegir dentro de un
            // torrent y su cabecera lo decia, cosa que aqui no viene a cuento.
            brls::Application::pushActivity(new TorrentSelectionActivity(
                manager_, "", std::move(preview), import.mode,
                settings_->get().streamSelection, {}, import,
                {}, "gamehubnx/torrent/title_shop"));
            return;
        }

        std::string taskId;
        std::string importError;
        if (!manager_->importDebrid(import, taskId, importError)) {
            operationMessage_ = importError;
            brls::Application::notify(importError);
        } else {
            // Clave propia: la de debrid promete que "los metadatos pueden
            // seguir obteniendose", y en una descarga directa no queda nada
            // pendiente -- el nombre y el tamano se supieron antes de encolar.
            statusLabel_->setText(tr("gamehubnx/detail/shop_queued"));
            if (onChange_)
                onChange_();
        }
        refreshButtons();
    }

    void startDebridInstall(TransferMode mode, bool forcePicker = false) {
        if (busy_ || !ensureDebridLinked(settings_, manager_))
            return;
        setBusy(true);
        cancelled_->store(false);
        primary_->setState(brls::ButtonState::DISABLED);
        secondary_->setState(brls::ButtonState::DISABLED);
        primary_->setText(tr("gamehubnx/debrid/submitting"));
        statusLabel_->setText(tr("gamehubnx/debrid/sending_magnet"));

        auto alive = alive_;
        auto cancelled = cancelled_;
        const AppSettingsData values = settings_->get();
        const DebridProviderKind providerKind = values.debridProvider;
        const std::string key = activeDebridKey(values);
        const CatalogEntry entry = entry_;
        brls::async([this, alive, cancelled, providerKind, key, entry, mode,
                     forcePicker] {
            auto provider = makeDebridProvider(providerKind, key);
            std::string debridId;
            std::string error;
            DebridInfo info;
            bool ok = provider->createFromMagnet(entry.magnetUri, debridId,
                                                  error);
            if (ok) {
                const auto deadline = std::chrono::steady_clock::now() +
                                      std::chrono::seconds(60);
                do {
                    std::string fetchError;
                    if (!provider->fetchInfo(debridId, info, fetchError))
                        error = std::move(fetchError);
                    log_msg("[DEBUG-debrid-picker] poll id=%s files=%u\n",
                            debridId.c_str(),
                            static_cast<unsigned>(info.files.size()));
                    if (!forcePicker || !info.files.empty())
                        break;
                    for (int i = 0; i < 8 && alive->load() &&
                                         !cancelled->load(); ++i)
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(250));
                } while (alive->load() && !cancelled->load() &&
                         std::chrono::steady_clock::now() < deadline);
                if (forcePicker && info.files.empty() && alive->load() &&
                    !cancelled->load()) {
                    ok = false;
                    if (error.empty())
                        error = "Unable to resolve torrent metadata.";
                }
            }
            brls::sync([this, alive, cancelled, ok, error, debridId, info,
                        entry, providerKind, key, mode, forcePicker] {
                if (!alive->load() || cancelled->load()) {
                    if (!debridId.empty())
                        removeDebridTransferAsync(providerKind, key, debridId);
                    if (alive->load()) {
                        setBusy(false);
                        refreshButtons();
                    }
                    return;
                }
                setBusy(false);
                if (!ok) {
                    if (!debridId.empty())
                        removeDebridTransferAsync(providerKind, key, debridId);
                    operationMessage_ = error.empty()
                        ? tr("gamehubnx/debrid/magnet_rejected") : error;
                    refreshButtons();
                    brls::Application::notify(operationMessage_);
                    return;
                }
                DebridImport import;
                import.infoHash = catalogLower(entry.infoHash);
                import.name = info.name.empty() ? entry.title : info.name;
                import.totalBytes = info.bytes ? info.bytes : entry.size;
                import.provider = providerKind;
                import.debridId = debridId;
                import.mode = mode;
                if (forcePicker && !info.files.empty()) {
                    TorrentPreview preview;
                    preview.name = import.name;
                    preview.totalBytes = import.totalBytes;
                    preview.fileCount = static_cast<uint32_t>(info.files.size());
                    for (const DebridFile& file : info.files) {
                        const bool package = isPackageName(file.path);
                        preview.files.push_back({file.path, file.bytes, package,
                                                 isCompressedName(file.path),
                                                 isCartridgeName(file.path)});
                        preview.packageCount += package ? 1 : 0;
                        preview.cartridgeCount += isCartridgeName(file.path) ? 1 : 0;
                    }
                    StreamSelection selection = settings_->get().streamSelection;
                    log_msg("[DEBUG-debrid-picker] push id=%s files=%u\n",
                            debridId.c_str(), preview.fileCount);
                    brls::Application::pushActivity(new TorrentSelectionActivity(
                        manager_, "", std::move(preview),
                        TransferMode::StreamInstall, selection, {}, import,
                        [providerKind, key, debridId] {
                            removeDebridTransferAsync(providerKind, key, debridId);
                        }));
                    log_msg("[DEBUG-debrid-picker] push complete id=%s\n",
                            debridId.c_str());
                    return;
                }
                if (mode == TransferMode::StreamInstall && !info.files.empty()) {
                    TorrentPreview preview;
                    preview.name = import.name;
                    preview.totalBytes = import.totalBytes;
                    preview.fileCount = static_cast<uint32_t>(info.files.size());
                    for (const DebridFile& file : info.files) {
                        const bool package = isPackageName(file.path);
                        preview.files.push_back({file.path, file.bytes, package,
                                                 isCompressedName(file.path),
                                                 isCartridgeName(file.path)});
                        preview.packageCount += package ? 1 : 0;
                        preview.cartridgeCount += isCartridgeName(file.path) ? 1 : 0;
                    }
                    import.fileSelection = smartInstallMask(preview);
                    import.packageCount = 0;
                    for (uint8_t action : import.fileSelection) {
                        if (action == static_cast<uint8_t>(FileAction::Install))
                            ++import.packageCount;
                    }
                    if (import.packageCount == 0) {
                        operationMessage_ = tr("gamehubnx/detail/smart_open_options");
                        refreshButtons();
                        brls::Application::notify(operationMessage_);
                        brls::Application::pushActivity(new TorrentSelectionActivity(
                            manager_, "", std::move(preview),
                            TransferMode::StreamInstall,
                            settings_->get().streamSelection, {}, import,
                            [providerKind, key, debridId] {
                                removeDebridTransferAsync(providerKind, key, debridId);
                            }));
                        return;
                    }
                }
                std::string id;
                std::string importError;
                if (!manager_->importDebrid(import, id, importError)) {
                    removeDebridTransferAsync(providerKind, key, debridId);
                    operationMessage_ = importError;
                    brls::Application::notify(importError);
                } else {
                    statusLabel_->setText(tr("gamehubnx/debrid/queued"));
                    if (onChange_)
                        onChange_();
                }
                refreshButtons();
            });
        });
    }

    void finishImport(const std::string& path, bool forcePicker,
                      std::vector<uint8_t> initialPeers) {
        gamehubnx::TorrentPreview preview;
        std::string error;
        if (!DownloadManager::previewTorrent(path, preview, error)) {
            diagnostic_error("catalog", "preview", "error=%s",
                             error.c_str());
            operationMessage_ = error;
            refreshButtons();
            brls::Application::notify(error);
            ::unlink(path.c_str());
            return;
        }

        std::vector<uint8_t> actions = smartInstallMask(preview);
        const auto sized = gamehubnx::estimateInstallSpace(
            preview, actions, TransferMode::StreamInstall);
        if (!preview.files.empty() && !sized.overflow) {
            installBytes_ = sized.requiredBytes;
            sizeExact_ = true;
            refreshSizeMeter();
        }

        // Picker path: the per-file picker owns the temp file and unlinks it
        // on cancel. Each row chooses Skip, Download, or Install directly.
        if (forcePicker) {
            openSelection(path, std::move(preview), std::move(initialPeers));
            return;
        }

        // One-tap path. No installable packages -> open the picker in download
        // mode so the user is not left at a dead end.
        if (preview.packageCount == 0) {
            operationMessage_ = preview.cartridgeCount > 0
                ? tr("gamehubnx/detail/cartridge_only")
                : tr("gamehubnx/detail/no_installable");
            refreshButtons();
            brls::Application::notify(operationMessage_);
            openSelection(path, std::move(preview), std::move(initialPeers),
                          TransferMode::DownloadOnly);
            return;
        }

        const bool titleInstalled = installed_ && installed_->contains(titleId_);
        std::vector<uint8_t> mask = smartInstallMask(preview);
        bool hasInstall = false;
        for (uint8_t action : mask) {
            if (action == static_cast<uint8_t>(FileAction::Install)) {
                hasInstall = true;
                break;
            }
        }
        if (!hasInstall) {
            operationMessage_ = titleInstalled
                ? tr("gamehubnx/detail/smart_open_options")
                : tr("gamehubnx/detail/no_installable");
            refreshButtons();
            brls::Application::notify(operationMessage_);
            openSelection(path, std::move(preview), std::move(initialPeers));
            return;
        }

        std::string id;
        std::string err;
        const std::string destination = installDestinationLabel(
            settings_ ? installTargetFor(settings_->get().installLocation)
                      : manager_->installTarget());
        if (manager_->importTorrentActions(path, mask, id, err, initialPeers)) {
            log_msg("[catalog] imported torrent %s\n", id.c_str());
            if (titleInstalled) {
                statusLabel_->setText(
                    tr("gamehubnx/detail/smart_installing_update", destination));
                brls::Application::notify(
                    tr("gamehubnx/detail/smart_installing_update", destination));
            } else {
                statusLabel_->setText(
                    tr("gamehubnx/detail/smart_installing_base", destination));
                brls::Application::notify(
                    tr("gamehubnx/detail/smart_installing_base", destination));
            }
            if (onChange_)
                onChange_();
        } else if (catalogLower(err).find("already in the download manager") !=
                   std::string::npos) {
            statusLabel_->setText(tr("gamehubnx/detail/already_in_downloads"));
            if (onChange_)
                onChange_();
        } else {
            log_msg("[catalog] import failed from '%s': %s\n",
                    path.c_str(), err.c_str());
            diagnostic_error("catalog", "import", "error=%s", err.c_str());
            operationMessage_ = err;
            refreshButtons();
            brls::Application::notify(err);
        }
        ::unlink(path.c_str());
        refreshButtons();
    }

    void openSelection(const std::string& path,
                       gamehubnx::TorrentPreview preview,
                       std::vector<uint8_t> initialPeers,
                       TransferMode preferred = TransferMode::StreamInstall) {
        StreamSelection selection = settings_
            ? settings_->get().streamSelection : StreamSelection::AllFiles;
        brls::Application::pushActivity(new TorrentSelectionActivity(
            manager_, path, std::move(preview), preferred,
            selection, std::move(initialPeers)));
    }

    std::string installedVersionForTitle() const {
        if (!installed_ || !installed_->contains(titleId_))
            return {};
        for (const InstalledTitle& title : installed_->titles()) {
            if (catalogLower(title.titleId) == catalogLower(titleId_))
                return title.version;
        }
        return {};
    }

    // La version nueva la pone la TIENDA (las actualizaciones de este juego en
    // addOns_), no el indice de metadatos: ver shop_update_source.hpp.
    std::string latestVersionForEntry() const {
        return shopLatestVersion(addOns_, titleId_);
    }

    std::vector<std::string> installedDlcIds() const {
        return installed_ ? installed_->dlcTitleIds() : std::vector<std::string>();
    }

    std::vector<uint8_t> smartInstallMask(
        const TorrentPreview& preview) const {
        const bool titleInstalled = installed_ && installed_->contains(titleId_);
        return selectSmartInstallFiles(preview, titleInstalled,
                                       installedVersionForTitle(),
                                       latestVersionForEntry(), titleId_,
                                       installedDlcIds());
    }

    /* Actualizaciones y DLC de este juego que hay en la tienda. Los calcula
       la vista de catalogo, que es quien tiene el catalogo entero; aqui
       llegan ya filtrados. Vacio para entradas que no son de la tienda. */
    std::vector<CatalogEntry> addOns_;
    /* Cierto cuando el catalogo en memoria trae complementos de ALGUN juego.
       Falso significa "todavia no lo se" --los sub-indices no han llegado-- y
       entonces la fila de DLC se calla en vez de afirmar un cero sin
       comprobar. */
    bool catalogKnowsAddOns_ = false;
    AddOnsCallback onAddOns_;
    CatalogEntry entry_;
    CatalogPresentation presentation_;
    std::string lastFailure_;
    DownloadManager* manager_;
    GameMetadataService* metadata_;
    InstalledTitleService* installed_;
    AppSettings* settings_;
    ModIndexService* mods_ = nullptr;
    FavoritesService* favorites_ = nullptr;
    SwitchDeployService* deploy_ = nullptr;
    std::string titleId_;
    std::string playersFact_;
    std::string operationMessage_;
    FailureCallback onFailure_;
    ChangeCallback onChange_;
    CloseCallback onClose_;
    std::shared_ptr<std::atomic<bool>> alive_;
    // Destino elegido en el modal, consumido al construir el DebridImport.
    install::InstallStorageTarget installTarget_ =
        install::InstallStorageTarget::SdCard;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    brls::AppletFrame* frame_ = nullptr;
    InstallButton* primary_ = nullptr;
    brls::Button* addOns_button_ = nullptr;
    brls::Label* installContract_ = nullptr;
    brls::Button* secondary_ = nullptr;
    brls::Button* favorite_ = nullptr;
    StorageMeter* sizeMeter_ = nullptr;
    brls::Label* statusLabel_ = nullptr;
    uint64_t installBytes_ = 0;
    bool sizeExact_ = false;
    int storageTick_ = 0;
    brls::RepeatingTimer timer_;
    std::vector<DownloadTask> cache_;
    bool busy_ = false;
};

}  // namespace gamehubnx::ui
