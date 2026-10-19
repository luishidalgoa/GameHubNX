#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <borealis.hpp>

#include "app/app_settings.hpp"
#include "app/catalog_service.hpp"
#include "app/download_manager.hpp"
#include "app/game_metadata_service.hpp"
#include "app/game_update_install.hpp"
#include "app/game_update_service.hpp"
#include "app/installed_title_service.hpp"
#include "app/magnet_resolver.hpp"
#include "ui/catalog/catalog_helpers.hpp"
#include "ui/common/async_image.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/i18n.hpp"
#include "ui/installed/update_file_chooser.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui {

class InstalledCell : public brls::RecyclerCell {
public:
    using CheckOne = std::function<void(const std::string&, const std::string&)>;
    using InstallOne = std::function<void(const std::string&)>;
    using UninstallOne = std::function<void(InstalledTitle)>;

    explicit InstalledCell(bool allowUninstall = true) {
        setFocusable(true);
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setPadding(10, 18, 10, 18);
        setHeight(92);

        image_ = new AsyncRgbaImage();
        image_->setWidth(64);
        image_->setHeight(64);
        image_->setCornerRadius(8);
        image_->setMarginRight(16);
        image_->setScalingType(brls::ImageScalingType::FILL);
        addView(image_);

        auto* labels = new brls::Box(brls::Axis::COLUMN);
        labels->setGrow(1);
        title_ = new brls::Label();
        title_->setSingleLine(true);
        title_->setAutoAnimate(false);
        title_->setFontSize(21);
        subtitle_ = new brls::Label();
        subtitle_->setSingleLine(true);
        subtitle_->setAutoAnimate(false);
        subtitle_->setFontSize(15);
        subtitle_->setMarginTop(6);
        subtitle_->setTextColor(theme::textTertiary());
        labels->addView(title_);
        labels->addView(subtitle_);
        addView(labels);

        // Update-state chip (Q10 colours): right-aligned coloured label.
        chip_ = new brls::Label();
        chip_->setSingleLine(true);
        chip_->setAutoAnimate(false);
        chip_->setFontSize(13);
        chip_->setMarginLeft(12);
        chip_->setShrink(0.0f);
        addView(chip_);

        registerClickAction([this](brls::View*) {
            if (titleId_.empty())
                return true;
            if (currentState_ == GameUpdateState::UpdateAvailable &&
                onInstallOne_)
                onInstallOne_(titleId_);
            else if (onCheckOne_)
                onCheckOne_(titleId_, version_);
            return true;
        });
        addGestureRecognizer(new brls::TapGestureRecognizer(this));

        if (allowUninstall) {
            registerAction(tr("gamehubnx/installed/uninstall_action"),
                           brls::BUTTON_X, [this](brls::View*) {
                if (!titleId_.empty() && onUninstallOne_)
                    onUninstallOne_(storedTitle_);
                return true;
            });
        }
    }

    void setTitle(const InstalledTitle& title,
                  GameMetadataService* metadata) {
        storedTitle_ = title;
        title_->setText(title.name);
        titleId_ = title.titleId;
        publisher_ = title.publisher;
        version_ = title.version;
        updateSubtitle();
        setArtworkUrl(image_, metadata, title.iconPath, currentIconPath_,
                      imageState_);
    }

    void setResult(const GameUpdateResult* result, CheckOne onCheckOne,
                   InstallOne onInstallOne, UninstallOne onUninstallOne) {
        onCheckOne_ = std::move(onCheckOne);
        onInstallOne_ = std::move(onInstallOne);
        onUninstallOne_ = std::move(onUninstallOne);
        const GameUpdateState state =
            result ? result->state : GameUpdateState::NotChecked;
        currentState_ = state;
        currentFoundVersion_ = result ? result->foundVersion : std::string();
        updateSubtitle();
        // The base click action is registered with a generic "OK" hint; A
        // means something different per state, so the hint bar must say so.
        // updateActionHint rewrites the text of the existing action (the
        // click still routes through the tap handler), then repaints the bar.
        updateActionHint(
            brls::BUTTON_A,
            state == GameUpdateState::UpdateAvailable
                ? tr("gamehubnx/common/install")
                : tr("gamehubnx/installed/action_check"));
        switch (state) {
        case GameUpdateState::UpdateAvailable:
            chip_->setText(tr("gamehubnx/installed/update_chip_available"));
            chip_->setTextColor(theme::warning());
            break;
        case GameUpdateState::UpToDate:
            chip_->setText(tr("gamehubnx/installed/update_chip_latest"));
            chip_->setTextColor(theme::success());
            break;
        case GameUpdateState::CheckError:
            chip_->setText(tr("gamehubnx/installed/update_chip_error"));
            chip_->setTextColor(theme::error());
            break;
        case GameUpdateState::SourceUnknown:
            chip_->setText(tr("gamehubnx/installed/update_chip_no_source"));
            chip_->setTextColor(theme::textTertiary());
            break;
        case GameUpdateState::NotChecked:
        case GameUpdateState::Checking:
        default:
            chip_->setText(tr("gamehubnx/installed/update_chip_not_checked"));
            chip_->setTextColor(theme::textTertiary());
            break;
        }
    }

    void onFocusGained() override {
        brls::RecyclerCell::onFocusGained();
        title_->setAnimated(true);
    }

    void onFocusLost() override {
        brls::RecyclerCell::onFocusLost();
        title_->setAnimated(false);
    }

private:
    // "Publisher · vX → vY": the version transition is the one fact this line
    // exists for when an update is available, so it must never be pushed off
    // the end. The 16-hex title ID was dropped — nothing on screen can act on
    // it, and on long publishers it was the first thing to get clipped.
    void updateSubtitle() {
        std::string subtitle;
        if (!publisher_.empty())
            subtitle = publisher_ + " · ";
        if (!version_.empty()) {
            subtitle += "v" + version_;
            if (currentState_ == GameUpdateState::UpdateAvailable &&
                !currentFoundVersion_.empty())
                subtitle += " → v" + currentFoundVersion_;
        }
        subtitle_->setText(subtitle);
    }

    AsyncRgbaImage* image_ = nullptr;
    brls::Label* title_ = nullptr;
    brls::Label* subtitle_ = nullptr;
    brls::Label* chip_ = nullptr;
    InstalledTitle storedTitle_;
    std::string currentIconPath_;
    std::shared_ptr<ImageRequestState> imageState_ =
        std::make_shared<ImageRequestState>();
    std::string titleId_;
    std::string publisher_;
    std::string version_;
    std::string currentFoundVersion_;
    GameUpdateState currentState_ = GameUpdateState::NotChecked;
    CheckOne onCheckOne_;
    InstallOne onInstallOne_;
    UninstallOne onUninstallOne_;
};

class InstalledDataSource : public brls::RecyclerDataSource {
public:
    explicit InstalledDataSource(GameMetadataService* metadata)
        : metadata_(metadata) {}

    void setTitles(std::vector<InstalledTitle> titles) {
        titles_ = std::move(titles);
    }

    void setResults(const GameUpdateResults* results) { results_ = results; }
    void setCheckOne(InstalledCell::CheckOne onCheckOne) {
        onCheckOne_ = std::move(onCheckOne);
    }
    void setInstallOne(InstalledCell::InstallOne onInstallOne) {
        onInstallOne_ = std::move(onInstallOne);
    }
    void setUninstallOne(InstalledCell::UninstallOne onUninstallOne) {
        onUninstallOne_ = std::move(onUninstallOne);
    }

    const std::vector<InstalledTitle>& titles() const { return titles_; }

    int numberOfRows(brls::RecyclerFrame*, int) override {
        return static_cast<int>(titles_.size());
    }

    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler,
                                    brls::IndexPath index) override {
        const InstalledTitle& title = titles_[static_cast<size_t>(index.row)];
        auto* cell = static_cast<InstalledCell*>(
            recycler->dequeueReusableCell("Installed"));
        cell->setTitle(title, metadata_);
        const GameUpdateResult* result = nullptr;
        if (results_) {
            auto it = results_->find(title.titleId);
            if (it != results_->end())
                result = &it->second;
        }
        cell->setResult(result, onCheckOne_, onInstallOne_, onUninstallOne_);
        return cell;
    }

private:
    GameMetadataService* metadata_;
    std::vector<InstalledTitle> titles_;
    const GameUpdateResults* results_ = nullptr;
    InstalledCell::CheckOne onCheckOne_;
    InstalledCell::InstallOne onInstallOne_;
    InstalledCell::UninstallOne onUninstallOne_;
};

class InstalledView : public brls::Box {
public:
    enum class Mode {
        Library,
        Updates,
    };

    InstalledView(InstalledTitleService* installed, DownloadManager* manager,
                  GameMetadataService* metadata, AppSettings* settings,
                  CatalogService* catalog, GameUpdateService* updates,
                  bool checkOnEntry = true, Mode mode = Mode::Library)
        : brls::Box(brls::Axis::COLUMN), installed_(installed),
          manager_(manager), metadata_(metadata), settings_(settings),
          catalog_(catalog), updates_(updates), checkOnEntry_(checkOnEntry),
          mode_(mode),
          alive_(std::make_shared<std::atomic<bool>>(true)) {
        status_ = new brls::Label();
        status_->setFontSize(15);
        status_->setMarginTop(10);
        status_->setMarginLeft(34);
        status_->setTextColor(theme::textTertiary());
        addView(status_);

        recycler_ = new brls::RecyclerFrame();
        recycler_->setGrow(1);
        recycler_->setPadding(6, 32, 6, 32);
        recycler_->estimatedRowHeight = 92;
        recycler_->registerCell("Installed", [this] {
            return new InstalledCell(mode_ == Mode::Library);
        });
        dataSource_ = new InstalledDataSource(metadata);
        recycler_->setDataSource(dataSource_);
        // Visibility toggles on the host, not the recycler: the host is the
        // grow(1) box, so hiding only the recycler would leave its slot behind.
        recyclerHost_ = recyclerHost(recycler_);
        addView(recyclerHost_);

        dataSource_->setResults(&updates_->results());
        dataSource_->setCheckOne(
            [this](const std::string& titleId, const std::string& version) {
                checkOneTitle(titleId, version);
            });
        dataSource_->setInstallOne(
            [this](const std::string& titleId) { installUpdate(titleId); });
        dataSource_->setUninstallOne(
            [this](InstalledTitle title) { confirmUninstall(std::move(title)); });
        reload();
        recheckTimer_.setCallback([this] { pollUpdateRecheck(); });
        // Entering the tab re-checks every installed title so verdicts are
        // fresh without pressing L. Skipped when nothing is installed and
        // disabled in the golden runner, which pins planted fixture states.
        if (checkOnEntry_ && !installed_->titles().empty())
            checkAllTitles();

        registerAction(tr("gamehubnx/installed/update_check_all"),
                       brls::BUTTON_LB, [this](brls::View*) {
            checkAllTitles();
            return true;
        });
        if (mode_ == Mode::Updates) {
            registerAction(tr("gamehubnx/updates/update_all"),
                           brls::BUTTON_X, [this](brls::View*) {
                queueAllUpdates();
                return true;
            });
        }
    }

    ~InstalledView() override {
        recheckTimer_.stop();
        alive_->store(false);
        // Abort an in-flight magnet resolve: without this, tearing the tab
        // down leaves the resolver hammering the network to completion.
        cancelled_->store(true);
    }

private:
    EmptyStateView* ensureEmptyState() {
        if (emptyState_)
            return emptyState_;
        emptyState_ = new EmptyStateView();
        if (mode_ == Mode::Updates) {
            emptyState_->setContent(
                tr("gamehubnx/updates/empty_title"),
                tr("gamehubnx/updates/empty_body"),
                tr("gamehubnx/installed/update_check_all"),
                [this] { checkAllTitles(); });
        } else {
            emptyState_->setContent(
                tr("gamehubnx/installed/empty_title"),
                tr("gamehubnx/installed/empty_body"),
                tr("gamehubnx/installed/refresh_action"),
                [this] { refresh(); });
        }
        addView(emptyState_);
        return emptyState_;
    }

    bool hasActiveStreamInstall() const {
        for (const DownloadTask& task : manager_->snapshot()) {
            if (task.mode != TransferMode::StreamInstall)
                continue;
            if (task.status == DownloadStatus::Queued ||
                task.status == DownloadStatus::Checking ||
                task.status == DownloadStatus::Fetching ||
                task.status == DownloadStatus::Downloading ||
                task.status == DownloadStatus::Installing ||
                task.status == DownloadStatus::Committing ||
                task.status == DownloadStatus::Verifying)
                return true;
        }
        return false;
    }

    // "Проверить всё" (LB): synchronous in-memory check of every installed
    // title, then persist and re-render. Re-entrancy is guarded by the
    // service itself; the work is microseconds, so no spinner is shown.
    void checkAllTitles() {
        std::string saveError;
        updates_->checkAll(installed_->titles(), installed_->generation(),
                           settings_->get().lastCatalogRefreshMs, saveError);
        if (!saveError.empty())
            diagnostic_error("game_updates", "save", "error=%s",
                             saveError.c_str());
        reload();
    }

    // "Проверить" на отдельное приложение (A-тап по строке).
    void checkOneTitle(const std::string& titleId,
                       const std::string& version) {
        std::string saveError;
        updates_->checkOne(titleId, version, saveError);
        if (!saveError.empty())
            diagnostic_error("game_updates", "save", "error=%s",
                             saveError.c_str());
        reload();
    }

    // A-тап по строке "Update available": скачать и установить апдейт.
    void installUpdate(const std::string& titleId) {
        pendingUpdateAll_.clear();
        updateAllAutoImport_ = false;
        if (refreshing_ || updateInFlight_ || uninstallInFlight_)
            return;
        uint64_t version = 0;
        const CatalogEntry* shop =
            shopUpdateFor(titleId, std::string(), &version);
        if (!shop) {
            brls::Application::notify(
                tr("gamehubnx/installed/update_no_bundle"));
            return;
        }
        confirmUpdateInstall(*shop, version);
    }

    void queueAllUpdates() {
        if (mode_ != Mode::Updates)
            return;
        if (refreshing_ || updateInFlight_ || uninstallInFlight_)
            return;
        pendingUpdateAll_.clear();
        for (const InstalledTitle& title : dataSource_->titles())
            pendingUpdateAll_.push_back(title.titleId);
        if (pendingUpdateAll_.empty()) {
            brls::Application::notify(tr("gamehubnx/updates/update_all_empty"));
            return;
        }
        updateAllAutoImport_ = true;
        brls::Application::notify(
            tr("gamehubnx/updates/update_all_started",
               pendingUpdateAll_.size()));
        pumpUpdateAll();
    }

    void pumpUpdateAll() {
        if (updateInFlight_ || uninstallInFlight_ || refreshing_)
            return;
        while (!pendingUpdateAll_.empty()) {
            const std::string titleId = pendingUpdateAll_.front();
            pendingUpdateAll_.erase(pendingUpdateAll_.begin());
            if (const CatalogEntry* shop =
                    shopUpdateFor(titleId, std::string())) {
                queueShopUpdate(*shop);
                return;
            }
            brls::Application::notify(
                tr("gamehubnx/installed/update_no_bundle"));
        }
        updateAllAutoImport_ = false;
        reload();
    }

    void confirmUninstall(InstalledTitle title) {
        if (refreshing_ || updateInFlight_ || uninstallInFlight_)
            return;
        if (hasActiveStreamInstall()) {
            brls::Application::notify(tr("gamehubnx/installed/busy"));
            return;
        }
        auto* dialog = new brls::Dialog(
            tr("gamehubnx/installed/uninstall_confirm", title.name));
        dialog->addButton(tr("gamehubnx/installed/uninstall_action"),
                          [this, title = std::move(title)] {
            beginUninstall(title);
        });
        dialog->addButton(tr("gamehubnx/common/cancel"), [] {});
        dialog->open();
    }

    void beginUninstall(const InstalledTitle& title) {
        if (uninstallInFlight_)
            return;
        uninstallInFlight_ = true;
        status_->setText(tr("gamehubnx/installed/uninstalling", title.name));
        auto alive = alive_;
        InstalledTitleService* installed = installed_;
        const std::string titleId = title.titleId;
        brls::async([this, alive, installed, titleId] {
            std::string error;
            std::string refreshError;
            const bool ok = installed->uninstall(titleId, error, refreshError);
            brls::sync([this, alive, ok, error, refreshError] {
                if (!alive->load())
                    return;
                uninstallInFlight_ = false;
                if (!ok) {
                    status_->setText(error);
                    brls::Application::notify(error);
                    return;
                }
                brls::Application::notify(
                    tr("gamehubnx/installed/uninstall_done"));
                if (!refreshError.empty()) {
                    status_->setText(refreshError);
                    brls::Application::notify(refreshError);
                    return;
                }
                checkAllTitles();
            });
        });
    }

    void confirmUpdateInstall(const CatalogEntry& update, uint64_t version) {
        auto* dialog = new brls::Dialog(tr(
            "gamehubnx/installed/update_install_confirm", update.title,
            std::to_string(version)));
        dialog->addButton(tr("gamehubnx/installed/update_download"),
                          [this, update] { queueShopUpdate(update); });
        dialog->addButton(tr("gamehubnx/common/later"), [] {});
        dialog->open();
    }

    // La actualizacion en la tienda, si esta.
    //
    // El servidor publica las actualizaciones en /api/shop/updates con su
    // enlace directo, y la ficha de un juego ya las instala por ahi. Esta
    // pestana no: seguia resolviendo un magnet y buscando peers, que es el
    // flujo del proyecto del que viene este fork. En una biblioteca propia no
    // hay enjambre que encontrar, asi que se quedaba en "buscando pares" para
    // siempre.
    //
    // El emparejado es el mismo que usa el catalogo para los complementos, y
    // por el mismo orden de fiabilidad: gh_id primero --lo sabe el servidor
    // desde que escaneo-- y el Title ID base como respaldo.
    const CatalogEntry* shopUpdateFor(const GameMetadata& entry) const {
        return shopUpdateFor(entry.titleId, entry.infoHash);
    }

    // La misma busqueda partiendo de lo que hay INSTALADO, que es lo unico que
    // conoce la pestana de actualizaciones: ahi no hay ficha de catalogo, solo
    // el Title ID que devuelve la consola. `versionOut` recibe la version de la
    // que se ha elegido, para poder compararla con la instalada.
    const CatalogEntry* shopUpdateFor(const std::string& titleId,
                                      const std::string& infoHash,
                                      uint64_t* versionOut = nullptr) const {
        if (versionOut)
            *versionOut = 0;
        if (!catalog_)
            return nullptr;
        const CatalogEntry* base =
            infoHash.empty() ? nullptr : catalog_->findByInfoHash(infoHash);
        const std::string baseId = gamehub::baseTitleIdOf(titleId);
        const CatalogEntry* mejor = nullptr;
        uint64_t mejorVersion = 0;
        for (const CatalogEntry& other : catalog_->entries()) {
            if (other.directUrl.empty())
                continue;                   // no es de la tienda
            if (!other.fromSubIndex)
                continue;                   // no es un complemento
            if (!gamehub::isUpdateTitleId(other.titleId))
                continue;                   // es un DLC, no la actualizacion
            bool mismo = false;
            if (base && base->gameId != 0 && other.gameId != 0)
                mismo = other.gameId == base->gameId;
            else if (!baseId.empty())
                mismo = gamehub::baseTitleIdOf(other.titleId) == baseId;
            if (!mismo)
                continue;
            // La tienda puede servir VARIAS actualizaciones del mismo juego
            // --Just Dance 2023 tiene tres, una de ellas la v65536-- y ofrecer
            // una vieja es peor que no ofrecer ninguna: se instala, no falla, y
            // deja el juego por detras de donde estaba. Se elige la mas alta.
            uint64_t suya = 0;
            if (!fileVersionTag(gamehub::fileNameFromUrl(other.directUrl),
                                suya))
                continue;   // sin etiqueta no hay forma de ordenarla
            if (!mejor || suya > mejorVersion) {
                mejor = &other;
                mejorVersion = suya;
            }
        }
        if (versionOut)
            *versionOut = mejorVersion;
        return mejor;
    }

    // Encolar la actualizacion de la tienda: un fichero, enlace directo, nada
    // que resolver ni enjambre al que preguntar. Es el mismo camino que ya usa
    // la ficha de un juego para sus complementos.
    void queueShopUpdate(const CatalogEntry& update) {
        DebridImport import;
        import.infoHash = catalogLower(update.infoHash);
        import.name = update.title;
        import.totalBytes = update.size;
        import.debridId = update.transferHandle();
        // Un parche es un paquete: se instala, no se deja suelto en la tarjeta.
        import.mode = TransferMode::StreamInstall;
        import.fileSelection = {static_cast<uint8_t>(FileAction::Install)};
        import.packageCount = 1u;
        import.hasInstallTarget = true;
        import.installTarget = settings_
            ? installTargetFor(settings_->get().installLocation)
            : manager_->installTarget();

        std::string taskId;
        std::string error;
        if (!manager_->importDebrid(import, taskId, error)) {
            status_->setText(error);
            brls::Application::notify(error);
            return;
        }
        status_->setText(tr("gamehubnx/detail/shop_queued"));
    }

    // UI-thread tick while an update task we started is in flight: wait for
    // a terminal state, then refresh installed titles and re-check them.
    void pollUpdateRecheck() {
        if (pendingRecheckTaskId_.empty())
            return;
        bool found = false;
        DownloadStatus status = DownloadStatus::Queued;
        for (const DownloadTask& candidate : manager_->snapshot()) {
            if (catalogLower(candidate.id) == pendingRecheckTaskId_) {
                found = true;
                status = candidate.status;
                break;
            }
        }
        if (!updateRecheckSettled(found, status))
            return;
        // Another stream install still running: the installed scan would
        // race it (same reason RB refresh refuses), keep polling.
        if (hasActiveStreamInstall() || refreshing_)
            return;
        recheckTimer_.stop();
        pendingRecheckTaskId_.clear();
        recheckAfterInstall();
    }

    // Refresh the installed list, then re-check every title. Mirrors
    // refresh() but always re-checks on success; callers ensure no stream
    // install is active and no other refresh is in flight.
    void recheckAfterInstall() {
        refreshing_ = true;
        status_->setText(tr("gamehubnx/installed/refreshing"));
        auto alive = alive_;
        InstalledTitleService* installed = installed_;
        brls::async([this, alive, installed] {
            std::string error;
            const bool ok = installed->refresh(error);
            brls::sync([this, alive, ok, error] {
                if (!alive->load())
                    return;
                refreshing_ = false;
                if (!ok) {
                    status_->setText(error);
                    brls::Application::notify(error);
                    return;
                }
                checkAllTitles();
            });
        });
    }

    // Que juegos salen en la pestana de actualizaciones: los que la TIENDA
    // tiene mas nuevos que la consola. Todo sale del veredicto del check
    // (GameUpdateService con la tienda como unica fuente), asi que lo que se
    // lista es siempre lo que se puede descargar.
    std::vector<InstalledTitle> onlyWithUpdate(
        std::vector<InstalledTitle> titles) const {
        const GameUpdateResults& results = updates_->results();
        std::vector<InstalledTitle> filtered;
        filtered.reserve(titles.size());
        for (InstalledTitle& title : titles) {
            auto it = results.find(title.titleId);
            if (it != results.end() &&
                it->second.state == GameUpdateState::UpdateAvailable)
                filtered.push_back(std::move(title));
        }
        return filtered;
    }

    // Una version de titulo en decimal ("0", "131072"). Vacia o con cualquier
    // caracter que no sea digito, no vale.
    static bool parseVersionTag(const std::string& text, uint64_t& out) {
        if (text.empty())
            return false;
        uint64_t value = 0;
        for (char c : text) {
            if (c < '0' || c > '9')
                return false;
            value = value * 10 + static_cast<uint64_t>(c - '0');
        }
        out = value;
        return true;
    }

    void reload() {
        std::vector<InstalledTitle> titles = installed_->titles();
        if (mode_ == Mode::Updates)
            titles = onlyWithUpdate(std::move(titles));
        size_t count = titles.size();
        dataSource_->setTitles(std::move(titles));
        recycler_->reloadData();
        // reloadData re-renders the focused row, whose A hint may have
        // changed state with it; neither setResult nor updateActionHint fires
        // the hints event, so repaint the bar once here — not per cell on
        // every draw (focus changes repaint it themselves).
        brls::Application::getGlobalHintsUpdateEvent()->fire();
        const bool empty = count == 0;
        if (empty)
            ensureEmptyState()->setVisibility(brls::Visibility::VISIBLE);
        else if (emptyState_)
            emptyState_->setVisibility(brls::Visibility::GONE);
        recyclerHost_->setVisibility(empty ? brls::Visibility::GONE
                                           : brls::Visibility::VISIBLE);
        std::string text = mode_ == Mode::Updates
            ? tr("gamehubnx/updates/count", count)
            : tr("gamehubnx/installed/count", count);
        if (updates_->stale(installed_->generation(),
                            settings_->get().lastCatalogRefreshMs))
            text += "   " + tr("gamehubnx/installed/update_stale");
        status_->setText(text);
    }

    void refresh() {
        if (refreshing_)
            return;
        if (hasActiveStreamInstall()) {
            brls::Application::notify(
                tr("gamehubnx/installed/busy"));
            return;
        }
        refreshing_ = true;
        status_->setText(tr("gamehubnx/installed/refreshing"));
        auto alive = alive_;
        InstalledTitleService* installed = installed_;
        brls::async([this, alive, installed] {
            std::string error;
            bool ok = installed->refresh(error);
            brls::sync([this, alive, ok, error] {
                if (!alive->load())
                    return;
                refreshing_ = false;
                if (!ok) {
                    status_->setText(error);
                    brls::Application::notify(error);
                    return;
                }
                reload();
            });
        });
    }

    InstalledTitleService* installed_;
    DownloadManager* manager_;
    GameMetadataService* metadata_ = nullptr;
    AppSettings* settings_;
    CatalogService* catalog_ = nullptr;
    GameUpdateService* updates_ = nullptr;
    bool checkOnEntry_ = true;
    Mode mode_ = Mode::Library;
    brls::Label* status_ = nullptr;
    EmptyStateView* emptyState_ = nullptr;
    brls::RecyclerFrame* recycler_ = nullptr;
    brls::Box* recyclerHost_ = nullptr;
    InstalledDataSource* dataSource_ = nullptr;
    std::shared_ptr<std::atomic<bool>> alive_;
    std::shared_ptr<std::atomic<bool>> cancelled_ =
        std::make_shared<std::atomic<bool>>(false);
    std::atomic<uint32_t> updateTempSerial_{0};
    brls::RepeatingTimer recheckTimer_;
    std::string pendingRecheckTaskId_;
    std::vector<std::string> pendingUpdateAll_;
    bool updateAllAutoImport_ = false;
    bool refreshing_ = false;
    bool updateInFlight_ = false;
    bool uninstallInFlight_ = false;
    brls::ActionIdentifier updateCancelAction_ = ACTION_NONE;
};

}  // namespace gamehubnx::ui
