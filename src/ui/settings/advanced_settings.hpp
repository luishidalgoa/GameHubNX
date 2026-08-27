#pragma once

#include <sys/statvfs.h>

#include <functional>
#include <string>

#include <borealis.hpp>
#ifdef __SWITCH__
#include <switch.h>
#endif

#include "app/app_settings.hpp"
#include "app/catalog_service.hpp"
#include "app/download_manager.hpp"
#include "app/game_metadata_service.hpp"
#include "app/installed_title_service.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/i18n.hpp"
#include "ui/settings/settings_cells.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui {

// Advanced settings — the rare / troubleshooting controls that used to crowd the
// bottom of SettingsView. Pushed as its own activity so the main list stays
// short. A factory reset here also has to refresh the parent list's cells, which
// is what onReset() is for (see SettingsView).
class AdvancedSettingsActivity : public brls::Activity {
public:
    AdvancedSettingsActivity(AppSettings* settings, DownloadManager* manager,
                             CatalogService* catalog,
                             GameMetadataService* metadata,
                             InstalledTitleService* installed,
                             std::function<void()> onReset)
        : settings_(settings), manager_(manager), catalog_(catalog),
          metadata_(metadata), installed_(installed),
          onReset_(std::move(onReset)) {
        auto* content = new brls::Box(brls::Axis::COLUMN);
        content->setPadding(24, 34, 24, 34);

        addSection(content, tr("gamehubnx/settings/section_network"));
        proxy_ = actionCell(tr("gamehubnx/settings/proxy"), "",
            [this] { editProxy(); });
        content->addView(proxy_);
        refreshProxyDetail();
        auto* proxyNote = new brls::Label();
        proxyNote->setText(tr("gamehubnx/settings/proxy_note"));
        proxyNote->setFontSize(16);
        proxyNote->setTextColor(theme::textSecondary());
        proxyNote->setMarginBottom(10);
        content->addView(proxyNote);

        addSection(content, tr("gamehubnx/settings/section_diagnostics"));
        auto* description = new brls::Label();
        description->setText(tr("gamehubnx/settings/diagnostics_note"));
        description->setFontSize(16);
        description->setTextColor(theme::textSecondary());
        description->setMarginBottom(10);
        content->addView(description);

        extendedTelemetry_ = new brls::BooleanCell();
        extendedTelemetry_->init(tr("gamehubnx/settings/extended_telemetry"),
            settings_->get().extendedTelemetry,
            [this](bool enabled) {
                AppSettingsData values = settings_->get();
                bool previous = values.extendedTelemetry;
                values.extendedTelemetry = enabled;
                if (!persist(values, "extended_telemetry")) {
                    extendedTelemetry_->setOn(previous, false);
                    return;
                }
                telemetry_set_enabled(enabled ? 1 : 0);
                brls::Application::notify(enabled
                    ? tr("gamehubnx/settings/telemetry_on")
                    : tr("gamehubnx/settings/telemetry_off"));
            });
        content->addView(extendedTelemetry_);

        content->addView(actionCell(tr("gamehubnx/settings/capture_snapshot"),
            tr("gamehubnx/settings/capture_snapshot_detail"),
            [this] { captureSnapshot(); }));
        content->addView(actionCell(tr("gamehubnx/settings/clear_log"),
            tr("gamehubnx/settings/clear_log_detail"),
            [this] { confirmClearLog(); }));
        content->addView(actionCell(tr("gamehubnx/settings/clear_artwork"),
            tr("gamehubnx/settings/clear_artwork_detail"),
            [this] { confirmClearArtwork(); }));
        content->addView(actionCell(tr("gamehubnx/settings/reset"),
            tr("gamehubnx/settings/reset_detail"),
            [this] { confirmReset(); }));

        auto* path = new brls::Label();
        path->setText(tr("gamehubnx/settings/log_path", LogPath));
        path->setFontSize(15);
        path->setTextColor(theme::textTertiary());
        path->setMarginTop(18);
        content->addView(path);

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1);
        scroll->setContentView(content);

        frame_ = new brls::AppletFrame(scroll);
        frame_->setTitle(tr("gamehubnx/settings/advanced"));
    }

    brls::View* createContentView() override {
        return frame_;
    }

private:
    void refreshProxyDetail() {
        const std::string& url = settings_->get().proxyUrl;
        proxy_->setDetailText(url.empty()
            ? tr("gamehubnx/settings/proxy_direct") : url);
    }

    void editProxy() {
        brls::Application::getImeManager()->openForText(
            [this](std::string text) {
                if (!gamehubnx::isValidProxyUrl(text)) {
                    brls::Application::notify(
                        tr("gamehubnx/settings/proxy_invalid"));
                    return;
                }
                AppSettingsData values = settings_->get();
                values.proxyUrl = text;
                if (!persist(values, "proxy_url"))
                    return;
                // Takes effect on the next request, not the next launch.
                gamehubnx::applyProxySetting(text);
                refreshProxyDetail();
            },
            tr("gamehubnx/settings/proxy"),
            tr("gamehubnx/settings/proxy_detail"), 128,
            settings_->get().proxyUrl, brls::KEYBOARD_DISABLE_NONE);
    }

    bool persist(const AppSettingsData& values, const char* tag) {
        std::string error;
        if (settings_->update(values, error))
            return true;
        diagnostic_error("settings", tag, "error=%s", error.c_str());
        brls::Application::notify(error);
        return false;
    }

    // Re-sync the cells this page owns after a reset restores defaults.
    void applyOwnValues() {
        const AppSettingsData& values = settings_->get();
        manager_->setInstallTarget(installTargetFor(values.installLocation));
        extendedTelemetry_->setOn(values.extendedTelemetry, false);
        telemetry_set_enabled(values.extendedTelemetry ? 1 : 0);
        // A reset clears the proxy, so the environment has to follow it.
        gamehubnx::applyProxySetting(values.proxyUrl);
        refreshProxyDetail();
    }

    void captureSnapshot() {
        writeSystemSnapshot(manager_, catalog_, metadata_, installed_, "manual");
        brls::Application::notify(tr("gamehubnx/settings/snapshot_written"));
    }

    void confirmClearLog() {
        auto* dialog = new brls::Dialog(
            tr("gamehubnx/settings/clear_log_question"));
        dialog->addButton(tr("gamehubnx/settings/clear_log"), [] {
            if (!clearApplicationLog())
                brls::Application::notify(
                    tr("gamehubnx/settings/clear_log_failed"));
            else
                brls::Application::notify(tr("gamehubnx/settings/clear_log_done"));
        });
        dialog->addButton(tr("gamehubnx/common/cancel"), [] {});
        dialog->open();
    }

    void confirmClearArtwork() {
        auto* dialog = new brls::Dialog(
            tr("gamehubnx/settings/clear_artwork_question"));
        dialog->addButton(tr("gamehubnx/settings/clear_artwork_action"), [this] {
            std::string error;
            if (!metadata_->clearImageCache(error)) {
                diagnostic_error("metadata", "clear_cache", "error=%s",
                                 error.c_str());
                brls::Application::notify(error);
            } else {
                brls::Application::notify(
                    tr("gamehubnx/settings/clear_artwork_done"));
            }
        });
        dialog->addButton(tr("gamehubnx/common/cancel"), [] {});
        dialog->open();
    }

    void confirmReset() {
        auto* dialog = new brls::Dialog(
            tr("gamehubnx/settings/reset_question"));
        dialog->addButton(tr("gamehubnx/settings/reset_action"), [this] {
            std::string error;
            if (!settings_->reset(error)) {
                diagnostic_error("settings", "reset", "error=%s",
                                 error.c_str());
                brls::Application::notify(error);
                return;
            }
            applyOwnValues();
            if (onReset_)
                onReset_();
            brls::Application::notify(tr("gamehubnx/settings/reset_done"));
        });
        dialog->addButton(tr("gamehubnx/common/cancel"), [] {});
        dialog->open();
    }

    AppSettings* settings_;
    DownloadManager* manager_;
    CatalogService* catalog_;
    GameMetadataService* metadata_;
    InstalledTitleService* installed_;
    std::function<void()> onReset_;
    brls::AppletFrame* frame_ = nullptr;
    brls::BooleanCell* extendedTelemetry_ = nullptr;
    brls::DetailCell* proxy_ = nullptr;
};

}  // namespace gamehubnx::ui
