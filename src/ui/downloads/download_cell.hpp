#pragma once

#include <memory>
#include <string>

#include <borealis.hpp>

#include "app/download_manager.hpp"
#include "app/switch_deploy.hpp"
#include "ui/common/async_image.hpp"
#include "ui/common/progress_bar.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/i18n.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui {

class DownloadCell : public brls::RecyclerCell {
public:
    DownloadCell() {
        setFocusable(true);
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setPadding(12, 20, 12, 20);
        setHeight(108);

        thumb_ = new brls::Box();
        thumb_->setWidth(72);
        thumb_->setHeight(72);
        thumb_->setCornerRadius(6);
        thumb_->setBackgroundColor(theme::surface());
        thumb_->setMarginRight(16);
        thumb_->setAlignItems(brls::AlignItems::CENTER);
        thumb_->setJustifyContent(brls::JustifyContent::CENTER);
        placeholder_ = new brls::Label();
        placeholder_->setFontSize(30);
        placeholder_->setTextColor(theme::textSecondary());
        thumb_->addView(placeholder_);
        image_ = new AsyncRgbaImage();
        image_->setWidth(72);
        image_->setHeight(72);
        image_->setPositionType(brls::PositionType::ABSOLUTE);
        image_->setPositionTop(0);
        image_->setPositionLeft(0);
        image_->setCornerRadius(6);
        image_->setScalingType(brls::ImageScalingType::FILL);
        thumb_->addView(image_);
        addView(thumb_);

        auto* right = new brls::Box(brls::Axis::COLUMN);
        right->setGrow(1);
        right->setJustifyContent(brls::JustifyContent::CENTER);

        auto* top = new brls::Box(brls::Axis::ROW);
        top->setAlignItems(brls::AlignItems::CENTER);
        title_ = new brls::Label();
        title_->setSingleLine(true);
        title_->setAutoAnimate(false);
        title_->setFontSize(22);
        title_->setGrow(1);
        status_ = new brls::Label();
        status_->setSingleLine(true);
        status_->setAutoAnimate(false);
        status_->setFontSize(17);
        status_->setTextColor(theme::accent());
        top->addView(title_);
        top->addView(status_);

        meta_ = new brls::Label();
        meta_->setSingleLine(true);
        meta_->setAutoAnimate(false);
        meta_->setFontSize(16);
        meta_->setMarginTop(6);
        meta_->setMarginBottom(9);

        progress_ = new ProgressBar();
        right->addView(top);
        right->addView(meta_);
        right->addView(progress_);
        addView(right);
    }

    void setTask(const DownloadTask& task, GameMetadataService* service,
                 const SwitchDeploySnapshot* deploy = nullptr) {
        setTextIfChanged(title_, task.name);
        setTextIfChanged(placeholder_, placeholderLetter(task.name));
        setTextIfChanged(status_, taskStatusText(task));
        status_->setTextColor(statusColor(task.status));
        const float progress = task.status == DownloadStatus::Fetching
            ? static_cast<float>(task.fetchProgress)
            : (task.status == DownloadStatus::Installing ||
               task.status == DownloadStatus::Committing)
                ? installProgressOf(task) : progressOf(task);
        progress_->setProgress(progress);

        const auto wanted = downloadProgressBytes(task);
        std::string meta = formatBytes(wanted.first) + " / " +
                           formatBytes(wanted.second);
        if (task.status == DownloadStatus::Installing ||
            task.status == DownloadStatus::Committing) {
            meta = tr("gamehubnx/downloads/cell_package",
                      task.packagesInstalled + 1, task.packageCount);
            if (!task.currentPackage.empty())
                meta += "   " + task.currentPackage;
            if (auto eta = taskEtaSeconds(task, now_ms()))
                meta += tr("gamehubnx/downloads/cell_eta",
                           formatEtaSeconds(*eta));
        } else if (task.status == DownloadStatus::Installed) {
            meta = tr("gamehubnx/downloads/cell_installed_packages",
                      task.packagesInstalled);
        } else if (task.status == DownloadStatus::Fetching) {
            // Con GameHub no hay servicio remoto preparando nada: fetchInfo
            // responde "listo" en el primer sondeo y makeProvider ya no
            // instancia ningun debrid. Decir "el servidor esta obteniendo
            // esto" es herencia del debrid y aqui es mentira.
            meta = task.source == TaskSource::Torrent
                ? tr("gamehubnx/downloads/cell_fetching",
                     percentOf(static_cast<float>(task.fetchProgress)))
                : tr("gamehubnx/downloads/cell_preparing");
        } else if (task.status == DownloadStatus::Downloading) {
            // Misma guarda que el detalle: el estado ya es Downloading aqui,
            // pero la muestra tambien tiene que ser reciente para no pintar
            // una velocidad que ya no esta pasando.
            meta += "   " + formatSpeed(currentDownloadSpeed(task, now_ms()));
            // Una descarga directa no tiene enjambre: "0 peers" no es un dato
            // pobre, es una mentira sobre como se esta bajando el fichero.
            if (task.source == TaskSource::Torrent)
                meta += tr("gamehubnx/downloads/cell_peers", task.peers);
            if (auto eta = taskEtaSeconds(task, now_ms()))
                meta += tr("gamehubnx/downloads/cell_eta",
                           formatEtaSeconds(*eta));
        } else if (task.status == DownloadStatus::Queued)
            meta += deploy && deploy->active() &&
                            task.mode == TransferMode::StreamInstall
                ? tr("gamehubnx/deploy/waiting_stream")
                : tr("gamehubnx/downloads/cell_waiting");
        else if (task.status == DownloadStatus::Error && !task.error.empty())
            meta += "   " + task.error;
        if (deploy && deploy->taskId == task.id) {
            if (deploy->active()) {
                const char* phaseKey =
                    deploy->phase == SwitchDeployPhase::Preparing
                        ? "gamehubnx/deploy/phase_preparing"
                        : deploy->phase == SwitchDeployPhase::Extracting
                              ? "gamehubnx/deploy/phase_extracting"
                              : "gamehubnx/deploy/phase_copying";
                setTextIfChanged(status_, tr(phaseKey));
                status_->setTextColor(theme::accent());
                progress_->setProgress(deploy->totalBytes
                    ? static_cast<float>(deploy->bytesCopied) /
                          static_cast<float>(deploy->totalBytes)
                    : 0.0f);
                meta = tr("gamehubnx/deploy/cell_progress",
                          percentOf(deploy->totalBytes
                                        ? static_cast<float>(deploy->bytesCopied) /
                                              static_cast<float>(deploy->totalBytes)
                                        : 0.0f),
                          deploy->filesCopied, deploy->totalFiles,
                          formatBytes(deploy->bytesCopied),
                          formatBytes(deploy->totalBytes));
                if (!deploy->currentPath.empty())
                    meta += "   " + deploy->currentPath;
            } else if (deploy->phase == SwitchDeployPhase::Completed) {
                setTextIfChanged(status_, tr("gamehubnx/deploy/completed"));
                status_->setTextColor(theme::success());
            } else if (deploy->phase == SwitchDeployPhase::Failed) {
                setTextIfChanged(status_, tr("gamehubnx/deploy/failed"));
                status_->setTextColor(theme::error());
                if (!deploy->detail.empty())
                    meta = deploy->detail;
            } else if (deploy->phase == SwitchDeployPhase::Cancelled) {
                setTextIfChanged(status_, tr("gamehubnx/deploy/cancelled"));
            }
        }
        setTextIfChanged(meta_, meta);

        std::string iconUrl;
        if (service) {
            const GameMetadata* found = service->findByInfoHash(task.id);
            if (found)
                iconUrl = found->iconUrl;
        }
        setArtworkUrl(image_, service, iconUrl, currentIconUrl_, imageState_);
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
    brls::Box* thumb_;
    brls::Label* placeholder_;
    AsyncRgbaImage* image_;
    brls::Label* title_;
    brls::Label* status_;
    brls::Label* meta_;
    ProgressBar* progress_;
    std::string currentIconUrl_;
    std::shared_ptr<ImageRequestState> imageState_ =
        std::make_shared<ImageRequestState>();
};

}  // namespace gamehubnx::ui
