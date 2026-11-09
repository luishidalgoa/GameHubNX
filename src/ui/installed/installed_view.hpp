#pragma once

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

#include <borealis.hpp>

#include "app/app_settings.hpp"
#include "app/catalog_service.hpp"
#include "app/download_manager.hpp"
#include "app/game_metadata_service.hpp"
#include "app/game_update_install.hpp"
#include "app/addon_status.hpp"
#include "app/game_update_service.hpp"
#include "app/torrent_update_probe.hpp"
#include "app/torrent_update_source.hpp"
#include "app/installed_title_service.hpp"
#include "app/magnet_resolver.hpp"

extern "C" {
#include "core/net.h"
}
#include "ui/catalog/catalog_helpers.hpp"
#include "ui/common/async_image.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/detail/game_detail.hpp"
#include "ui/detail/shop_addons.hpp"
#include "ui/detail/shop_items.hpp"
#include "ui/i18n.hpp"
#include "ui/installed/update_file_chooser.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui {

// Lo que una fila de actualizaciones tiene pendiente, y sus complementos.
struct RowAddOns {
    RowAddOnKind kind = RowAddOnKind::None;
    std::vector<CatalogEntry> addOns;
};

class InstalledCell : public brls::RecyclerCell {
public:
    using CheckOne = std::function<void(const std::string&, const std::string&)>;
    using InstallOne = std::function<void(const std::string&)>;
    using UninstallOne = std::function<void(InstalledTitle)>;
    // Tocar el juego abre su ficha (biblioteca). Sin el, tocar comprueba o instala.
    using OpenOne = std::function<void(const InstalledTitle&)>;

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
            if (openOne_) {
                openOne_(storedTitle_);
                return true;
            }
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

    // El hint de la A depende de la pantalla: "Ver ficha" en la biblioteca,
    // "Que instalar" en actualizaciones.
    void setOpenOne(OpenOne onOpenOne, std::string hintKey) {
        openOne_ = std::move(onOpenOne);
        openHint_ = std::move(hintKey);
    }
    void setAddOnKind(RowAddOnKind kind) { addOnKind_ = kind; }

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
            openOne_ ? tr(openHint_.c_str())
            : state == GameUpdateState::UpdateAvailable
                ? tr("gamehubnx/common/install")
                : tr("gamehubnx/installed/action_check"));
        if (addOnKind_ == RowAddOnKind::Dlc ||
            addOnKind_ == RowAddOnKind::UpdateAndDlc) {
            chip_->setText(tr(addOnKind_ == RowAddOnKind::Dlc
                                  ? "gamehubnx/installed/chip_dlc"
                                  : "gamehubnx/installed/chip_update_dlc"));
            chip_->setTextColor(theme::warning());
            return;
        }
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
    OpenOne openOne_;
    std::string openHint_;
    RowAddOnKind addOnKind_ = RowAddOnKind::None;
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
    void setOpenOne(InstalledCell::OpenOne onOpenOne, std::string hintKey) {
        onOpenOne_ = std::move(onOpenOne);
        openHint_ = std::move(hintKey);
    }
    void setAddOns(const std::unordered_map<std::string, RowAddOns>* addOns) {
        addOns_ = addOns;
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
        cell->setOpenOne(onOpenOne_, openHint_);
        RowAddOnKind kind = RowAddOnKind::None;
        if (addOns_) {
            auto found = addOns_->find(title.titleId);
            if (found != addOns_->end())
                kind = found->second.kind;
        }
        cell->setAddOnKind(kind);
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
    InstalledCell::OpenOne onOpenOne_;
    std::string openHint_;
    const std::unordered_map<std::string, RowAddOns>* addOns_ = nullptr;
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
                  bool checkOnEntry = true, Mode mode = Mode::Library,
                  ModIndexService* mods = nullptr,
                  FavoritesService* favorites = nullptr,
                  SwitchDeployService* deploy = nullptr,
                  TorrentUpdateSource* torrentUpdates = nullptr)
        : brls::Box(brls::Axis::COLUMN), installed_(installed),
          manager_(manager), metadata_(metadata), settings_(settings),
          catalog_(catalog), updates_(updates), checkOnEntry_(checkOnEntry),
          mode_(mode), mods_(mods), favorites_(favorites), deploy_(deploy),
          torrentUpdates_(torrentUpdates),
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
        // En la biblioteca tocar el juego abre su ficha; en actualizaciones
        // sigue comprobando o instalando.
        if (mode_ == Mode::Library)
            dataSource_->setOpenOne(
                [this](const InstalledTitle& title) { openDetail(title); },
                "gamehubnx/common/view_game");
        else
            dataSource_->setOpenOne(
                [this](const InstalledTitle& title) { openWhatToInstall(title); },
                "gamehubnx/installed/action_what_to_install");
        dataSource_->setAddOns(&rowAddOns_);
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
        if (mode_ == Mode::Updates && torrentUpdates_) {
            registerAction(tr("gamehubnx/updates/probe_torrents"),
                           brls::BUTTON_RB, [this](brls::View*) {
                probeTorrentsAll();
                return true;
            });
        }
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
            if (queueTorrentUpdate(titleId))
                return;
            brls::Application::notify(
                tr("gamehubnx/installed/update_no_bundle"));
            return;
        }
        confirmUpdateInstall(*shop, version);
    }

    // Busca, en los torrents de los juegos instalados que tienen magnet en la
    // tienda, las actualizaciones que traen. Corre en segundo plano: cada magnet
    // necesita peers. Al terminar se comprueba la version de cada juego.
    void probeTorrentsAll() {
        if (!torrentUpdates_ || probing_ || refreshing_ || updateInFlight_)
            return;
        std::vector<std::pair<std::string, std::string>> jobs;
        for (const InstalledTitle& title : installed_->titles()) {
            const std::string magnet = torrentUpdates_->magnetFor(title.titleId);
            if (!magnet.empty())
                jobs.emplace_back(title.titleId, magnet);
        }
        if (jobs.empty()) {
            brls::Application::notify(tr("gamehubnx/updates/probe_none"));
            return;
        }
        probing_ = true;
        status_->setText(tr("gamehubnx/updates/probe_running",
                            static_cast<int>(jobs.size())));
        auto alive = alive_;
        TorrentUpdateSource* source = torrentUpdates_;
        const std::string tmpDir = manager_->rootPath();
        brls::async([this, alive, source, jobs, tmpDir] {
            size_t done = 0;
            for (const auto& job : jobs) {
                std::string error;
                probeTorrentUpdates(*source, job.first, job.second, tmpDir,
                                    error);
                ++done;
                brls::sync([this, alive, done, total = jobs.size()] {
                    if (alive->load())
                        status_->setText(tr("gamehubnx/updates/probe_progress",
                                            static_cast<int>(done),
                                            static_cast<int>(total)));
                });
            }
            std::string saveError;
            source->save(saveError);
            brls::sync([this, alive] {
                if (!alive->load())
                    return;
                probing_ = false;
                checkAllTitles();
            });
        });
    }

    // Descarga la actualizacion de un juego desde su torrent: solo el fichero de
    // esa version, instalado como paquete. Devuelve false si no hay torrent o
    // version conocidos.
    bool queueTorrentUpdate(const std::string& titleId) {
        if (!torrentUpdates_ || !updates_)
            return false;
        const std::string magnet = torrentUpdates_->magnetFor(titleId);
        if (magnet.empty())
            return false;
        std::string latest;
        for (const auto& entry : updates_->results()) {
            if (catalogLower(entry.first) == catalogLower(titleId)) {
                latest = entry.second.foundVersion;
                break;
            }
        }
        if (latest.empty())
            return false;
        // Una sola busqueda de torrent a la vez: cada una abre su DHT y sus sockets,
        // y varias pulsaciones seguidas agotaban los sockets de la consola.
        if (torrentUpdateInFlight_) {
            /* Segunda pulsacion sobre la busqueda en curso: la cancela. */
            if (updateCancelled_)
                updateCancelled_->store(true);
            status_->setText(tr("gamehubnx/installed/update_cancelling"));
            return true;
        }
        torrentUpdateInFlight_ = true;
        auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
        updateCancelled_ = cancelFlag;
        status_->setText(tr("gamehubnx/detail/finding_peers") +
                         tr("gamehubnx/installed/update_progress_hint"));
        auto alive = alive_;
        const std::string tmpDir = manager_->rootPath();
        DownloadManager* manager = manager_;
        brls::async([this, alive, manager, magnet, titleId, latest, tmpDir,
                     cancelFlag] {
            const std::string tmp = tmpDir + "/_update_magnet.torrent";
            MagnetResolver resolver;
            std::string error;
            std::vector<uint8_t> peers;
            /* Progreso al estado de la fila. El callback corre en hilos del
               resolutor: la etiqueta solo se toca desde el hilo de la UI. */
            auto progress = [this, alive](const MagnetProgress& p) {
                std::string text;
                switch (p.stage) {
                    case MagnetProgress::Stage::FindingPeers:
                        text = p.peerCount
                                   ? tr("gamehubnx/installed/update_finding_dht",
                                        p.peerCount)
                                   : tr("gamehubnx/detail/finding_peers");
                        break;
                    case MagnetProgress::Stage::Connecting:
                        text = tr("gamehubnx/detail/contacting_peer",
                                  p.peerIndex, p.peerCount);
                        break;
                    case MagnetProgress::Stage::FetchingMetadata:
                        text = tr("gamehubnx/detail/fetching_metadata",
                                  p.completedPieces, p.totalPieces);
                        break;
                    case MagnetProgress::Stage::Validating:
                        text = tr("gamehubnx/detail/validating");
                        break;
                }
                text += tr("gamehubnx/installed/update_progress_hint");
                brls::sync([this, alive, text] {
                    if (!alive->load() || !torrentUpdateInFlight_)
                        return;
                    status_->setText(text);
                });
            };
            bool resolved = false;
            try {
                resolved = resolver.resolveToFile(magnet, tmp, *cancelFlag,
                                                  progress, error, &peers,
                                                  nullptr);
            } catch (...) {
                /* Antes, una excepcion aqui escapaba del hilo y cerraba la app. */
                error = tr("gamehubnx/installed/update_error_no_peers");
            }
            std::string taskId;
            std::string importError;
            bool imported = false;
            if (resolved) {
                TorrentPreview preview;
                if (DownloadManager::previewTorrent(tmp, preview, importError)) {
                    const std::vector<size_t> matches =
                        updateVersionMatches(preview, latest, titleId);
                    if (matches.empty()) {
                        importError = tr("gamehubnx/installed/update_no_bundle");
                    } else {
                        gh_diag_set_phase("updater:import");
                        imported = manager->importTorrentActions(
                            tmp, selectFiles(preview, matches), taskId,
                            importError, peers, "update:" + latest);
                    }
                }
            } else if (cancelFlag->load()) {
                importError = tr("gamehubnx/installed/update_cancelled");
            } else {
                importError = error;
            }
            ::unlink(tmp.c_str());
            brls::sync([this, alive, imported, taskId, importError] {
                if (!alive->load())
                    return;
                torrentUpdateInFlight_ = false;
                if (!imported) {
                    status_->setText(importError);
                    brls::Application::notify(importError);
                    return;
                }
                status_->setText(tr("gamehubnx/detail/shop_queued"));
            });
        });
        return true;
    }

    void queueAllUpdates() {
        if (mode_ != Mode::Updates)
            return;
        if (refreshing_ || updateInFlight_ || uninstallInFlight_)
            return;
        pendingUpdateAll_.clear();
        // Solo las filas con una actualizacion: un DLC suelto no tiene bundle.
        for (const InstalledTitle& title : dataSource_->titles()) {
            auto it = rowAddOns_.find(title.titleId);
            if (it != rowAddOns_.end() &&
                (it->second.kind == RowAddOnKind::Update ||
                 it->second.kind == RowAddOnKind::UpdateAndDlc))
                pendingUpdateAll_.push_back(title.titleId);
        }
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
    // Las filas de actualizaciones: las que tienen una actualizacion o DLC que la
    // consola no tiene. Un juego sin nada pendiente no aparece.
    std::vector<InstalledTitle> withPendingAddOns(
        std::vector<InstalledTitle> titles) {
        rowAddOns_.clear();
        const GameUpdateResults& results = updates_->results();
        std::vector<InstalledTitle> filtered;
        filtered.reserve(titles.size());
        for (InstalledTitle& title : titles) {
            auto it = results.find(title.titleId);
            const bool hasUpdate =
                it != results.end() &&
                it->second.state == GameUpdateState::UpdateAvailable;
            RowAddOns row;
            bool hasMissingDlc = false;
            if (const CatalogEntry* base = findCatalogEntry(title)) {
                AddOnSelection selection = addOnsOf(catalog_->entries(), *base);
                std::vector<std::string> catalogDlc;
                for (const CatalogEntry& addOn : selection.addOns)
                    if (gamehub::isDlcTitleId(addOn.titleId))
                        catalogDlc.push_back(addOn.titleId);
                hasMissingDlc = !missingDlcTitleIds(catalogDlc,
                                                    installed_->dlcTitleIds())
                                     .empty();
                row.addOns = std::move(selection.addOns);
            }
            row.kind = classifyRowAddOns(hasUpdate, hasMissingDlc);
            if (row.kind == RowAddOnKind::None)
                continue;
            rowAddOns_[title.titleId] = std::move(row);
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
            titles = withPendingAddOns(std::move(titles));
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

    // La entrada del catalogo que es el mismo juego base que el instalado. Los
    // DLC y actualizaciones (sub-indices) no cuentan: la ficha es la del juego.
    const CatalogEntry* findCatalogEntry(const InstalledTitle& title) const {
        if (!catalog_ || title.titleId.empty())
            return nullptr;
        const std::string base = gamehub::baseTitleIdOf(title.titleId);
        for (const CatalogEntry& entry : catalog_->entries()) {
            if (entry.fromSubIndex || !gamehub::isBaseTitleId(entry.titleId))
                continue;
            if (gamehub::baseTitleIdOf(entry.titleId) == base)
                return &entry;
        }
        return nullptr;
    }

    // "Que instalar" de la fila: el juego (si falta) y sus complementos pendientes.
    // Desde ahi, el modal de instalacion de siempre.
    void openWhatToInstall(const InstalledTitle& title) {
        const CatalogEntry* base = findCatalogEntry(title);
        auto it = rowAddOns_.find(title.titleId);
        if (!base || it == rowAddOns_.end()) {
            brls::Application::notify(tr("gamehubnx/installed/no_catalog_entry"));
            return;
        }
        std::vector<ShopItem> items =
            buildShopItems(installed_, *base, it->second.addOns);
        if (items.empty()) {
            // Una actualizacion que solo existe en torrent no tiene complementos de
            // tienda que elegir: abrir la lista dejaba una seccion DLC vacia con una
            // fila sin nombre y el boton Instalar apagado. Se instala directamente
            // (installUpdate cae en el torrent si no hay entrada de tienda).
            installUpdate(title.titleId);
            return;
        }
        brls::Application::pushActivity(new ShopAddOnsActivity(
            title.name, std::move(items), manager_, settings_));
    }

    void openDetail(const InstalledTitle& title) {
        const CatalogEntry* entry = findCatalogEntry(title);
        if (!entry) {
            brls::Application::notify(tr("gamehubnx/installed/no_catalog_entry"));
            return;
        }
        pushGameDetail(*entry, manager_, metadata_, installed_, settings_, mods_,
                       favorites_, deploy_);
    }

    InstalledTitleService* installed_;
    DownloadManager* manager_;
    GameMetadataService* metadata_ = nullptr;
    AppSettings* settings_;
    CatalogService* catalog_ = nullptr;
    GameUpdateService* updates_ = nullptr;
    bool checkOnEntry_ = true;
    Mode mode_ = Mode::Library;
    ModIndexService* mods_ = nullptr;
    FavoritesService* favorites_ = nullptr;
    SwitchDeployService* deploy_ = nullptr;
    TorrentUpdateSource* torrentUpdates_ = nullptr;
    bool probing_ = false;
    bool torrentUpdateInFlight_ = false;
    std::shared_ptr<std::atomic<bool>> updateCancelled_;
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
    std::unordered_map<std::string, RowAddOns> rowAddOns_;
    std::vector<std::string> pendingUpdateAll_;
    bool updateAllAutoImport_ = false;
    bool refreshing_ = false;
    bool updateInFlight_ = false;
    bool uninstallInFlight_ = false;
    brls::ActionIdentifier updateCancelAction_ = ACTION_NONE;
};

}  // namespace gamehubnx::ui
