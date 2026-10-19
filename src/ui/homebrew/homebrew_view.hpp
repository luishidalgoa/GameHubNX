#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <ctime>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "app/homebrew_loader.hpp"
#include "app/homebrew_source.hpp"
#include "ui/i18n.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui {

// Una app del catalogo y lo que la consola tiene instalado de ella.
struct HomebrewRow {
    HomebrewApp app;
    // Tag instalado, leido del fichero que deja la app. Vacio si no esta.
    std::string installedTag;
    // Hay un .nro en su sitio aunque no sepamos la version (lo puso otro).
    bool installedFileExists = false;
};

inline std::string lowerCopy(std::string text) {
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

inline bool homebrewFileExists(const std::string& path) {
    return std::ifstream(path).good();
}

// Estado de una fila: instalada, con actualizacion, o sin instalar.
enum class HomebrewState { NotInstalled, Installed, UpdateAvailable };

inline HomebrewState homebrewStateOf(const HomebrewRow& row) {
    const HomebrewRelease* latest = row.app.recommended();
    if (!row.installedFileExists && row.installedTag.empty())
        return HomebrewState::NotInstalled;
    if (latest && !row.installedTag.empty() &&
        compareHomebrewVersions(row.installedTag, latest->tag) < 0)
        return HomebrewState::UpdateAvailable;
    return HomebrewState::Installed;
}

class HomebrewCell : public brls::RecyclerCell {
public:
    HomebrewCell() {
        setFocusable(true);
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setPadding(8, 20, 8, 20);
        setHeight(76);

        auto* text = new brls::Box(brls::Axis::COLUMN);
        text->setGrow(1);
        name_ = new brls::Label();
        name_->setSingleLine(true);
        name_->setAutoAnimate(false);
        name_->setFontSize(theme::kFontBody);
        meta_ = new brls::Label();
        meta_->setSingleLine(true);
        meta_->setAutoAnimate(false);
        meta_->setFontSize(theme::kFontCaption);
        meta_->setTextColor(theme::textTertiary());
        meta_->setMarginTop(3);
        text->addView(name_);
        text->addView(meta_);
        addView(text);

        badge_ = new brls::Label();
        badge_->setFontSize(theme::kFontCaption);
        badge_->setMarginLeft(16);
        addView(badge_);
    }

    void setRow(const HomebrewRow& row) {
        const HomebrewRelease* latest = row.app.recommended();
        setTextIfChanged(name_, row.app.displayName());
        const std::string detail = row.app.author.empty()
                                       ? (latest ? latest->nroName : std::string())
                                       : row.app.author;
        setTextIfChanged(meta_, latest ? latest->tag + "  " + detail
                                       : tr("gamehubnx/homebrew/no_release"));
        switch (homebrewStateOf(row)) {
            case HomebrewState::NotInstalled:
                setTextIfChanged(badge_, tr("gamehubnx/homebrew/not_installed"));
                badge_->setTextColor(theme::textTertiary());
                break;
            case HomebrewState::Installed:
                setTextIfChanged(badge_, tr("gamehubnx/homebrew/installed",
                                            row.installedTag.empty()
                                                ? std::string("?")
                                                : row.installedTag));
                badge_->setTextColor(theme::success());
                break;
            case HomebrewState::UpdateAvailable:
                setTextIfChanged(badge_, tr("gamehubnx/homebrew/update",
                                            latest->tag));
                badge_->setTextColor(theme::accent());
                break;
        }
        setFocusable(latest != nullptr);
        name_->setTextColor(latest ? theme::textPrimary()
                                   : theme::textDisabled());
    }

private:
    brls::Label* name_;
    brls::Label* meta_;
    brls::Label* badge_;
};

class HomebrewView;

class HomebrewDataSource : public brls::RecyclerDataSource {
public:
    explicit HomebrewDataSource(HomebrewView* owner) : owner_(owner) {}

    void setRows(std::vector<HomebrewRow> rows) { rows_ = std::move(rows); }
    std::vector<HomebrewRow>& rows() { return rows_; }

    int numberOfRows(brls::RecyclerFrame*, int) override {
        return static_cast<int>(rows_.size());
    }
    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler,
                                   brls::IndexPath index) override {
        auto* cell =
            static_cast<HomebrewCell*>(recycler->dequeueReusableCell("Homebrew"));
        const size_t i = static_cast<size_t>(index.row);
        if (i < rows_.size())
            cell->setRow(rows_[i]);
        return cell;
    }
    float heightForRow(brls::RecyclerFrame*, brls::IndexPath) override {
        return 76.0f;
    }
    void didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) override;

private:
    HomebrewView* owner_;
    std::vector<HomebrewRow> rows_;
};

// Pestana de homebrew: lista los releases de GitHub de los repos conocidos y
// instala el .nro recomendado en la SD, sin pasar por ningun catalogo nuestro.
class HomebrewView : public brls::Box {
public:
    HomebrewView() : brls::Box(brls::Axis::COLUMN) {
        setGrow(1);
        setPadding(20, 32, 0, 32);

        status_ = new brls::Label();
        status_->setFontSize(theme::kFontSmall);
        status_->setTextColor(theme::textTertiary());
        status_->setMarginBottom(12);
        status_->setText(tr("gamehubnx/homebrew/loading"));
        addView(status_);

        recycler_ = new brls::RecyclerFrame();
        recycler_->setGrow(1);
        recycler_->estimatedRowHeight = 76;
        recycler_->registerCell("Homebrew", [] { return new HomebrewCell(); });
        dataSource_ = new HomebrewDataSource(this);
        recycler_->setDataSource(dataSource_);
        recyclerHost_ = recyclerHost(recycler_);
        recyclerHost_->setVisibility(brls::Visibility::GONE);
        addView(recyclerHost_);

        registerAction(tr("gamehubnx/homebrew/refresh"), brls::BUTTON_X,
                       [this](brls::View*) {
                           load(true);
                           return true;
                       });
        load(false);
    }

    ~HomebrewView() override { alive_->store(false); }

    // Ocupada mientras hay una descarga: no se encolan dos a la vez.
    bool busy() const { return busy_; }

    // Pregunta antes de instalar: escribir en la SD sin preguntar no se hace.
    void confirmInstall(size_t index) {
        auto& rows = dataSource_->rows();
        if (index >= rows.size() || busy_)
            return;
        const HomebrewRelease* release = rows[index].app.recommended();
        if (!release) {
            brls::Application::notify(tr("gamehubnx/homebrew/no_release"));
            return;
        }
        auto* dialog = new brls::Dialog(tr("gamehubnx/homebrew/install_confirm",
                                           rows[index].app.displayName(),
                                           release->tag));
        dialog->addButton(tr("gamehubnx/homebrew/install"),
                          [this, index] { install(index); });
        dialog->addButton(tr("gamehubnx/common/later"), [] {});
        dialog->open();
    }

private:
    // Carga en un hilo de trabajo: la red solo se toca si la cache caduco o si
    // se fuerza. Mientras carga no se encola otra carga.
    void load(bool force) {
        if (loading_ || busy_)
            return;
        loading_ = true;
        status_->setText(tr("gamehubnx/homebrew/loading"));
        auto alive = alive_;
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        brls::async([this, alive, force, now] {
            HomebrewSourceLoad result = loadHomebrewSources(force, now);
            brls::sync([this, alive, result = std::move(result)]() mutable {
                if (!alive->load())
                    return;
                loading_ = false;
                std::vector<HomebrewRow> rows;
                rows.reserve(result.apps.size());
                for (HomebrewApp& app : result.apps) {
                    HomebrewRow row;
                    row.app = std::move(app);
                    refreshInstalled(row);
                    rows.push_back(std::move(row));
                }
                if (rows.empty()) {
                    status_->setText(result.error.empty()
                        ? tr("gamehubnx/homebrew/empty") : result.error);
                    return;
                }
                if (result.staleAgeSec >= 0) {
                    status_->setText(tr("gamehubnx/homebrew/offline",
                                        std::to_string(result.staleAgeSec / 3600)));
                } else if (!result.error.empty()) {
                    status_->setText(result.error);
                } else {
                    status_->setText(tr("gamehubnx/homebrew/repos", rows.size()));
                }
                dataSource_->setRows(std::move(rows));
                recyclerHost_->setVisibility(brls::Visibility::VISIBLE);
                recycler_->reloadData();
            });
        });
    }

    // Lee del disco lo que hay instalado de esta app. Es un fichero pequeño y
    // solo se toca al cargar o al instalar, asi que va en el hilo de la UI.
    static void refreshInstalled(HomebrewRow& row) {
        const HomebrewRelease* latest = row.app.recommended();
        if (!latest) {
            row.installedTag.clear();
            row.installedFileExists = false;
            return;
        }
        const std::string path = homebrewNroPath(*latest);
        row.installedTag = installedHomebrewVersion(path);
        row.installedFileExists = homebrewFileExists(path);
    }

    void install(size_t index) {
        auto& rows = dataSource_->rows();
        if (index >= rows.size() || busy_)
            return;
        const HomebrewRelease release = *rows[index].app.recommended();
        busy_ = true;
        status_->setText(tr("gamehubnx/homebrew/downloading", release.nroName));
        auto alive = alive_;
        brls::async([this, alive, release, index] {
            std::string installedPath;
            std::string error;
            const bool ok = installHomebrewRelease(release, installedPath, error);
            brls::sync([this, alive, ok, error, release, index] {
                if (!alive->load())
                    return;
                busy_ = false;
                auto& rows = dataSource_->rows();
                if (ok && index < rows.size()) {
                    refreshInstalled(rows[index]);
                    brls::Application::notify(
                        tr("gamehubnx/homebrew/done", release.nroName));
                } else {
                    brls::Application::notify(
                        tr("gamehubnx/homebrew/failed", error));
                }
                status_->setText(tr("gamehubnx/homebrew/repos", rows.size()));
                recycler_->reloadData();
            });
        });
    }

    brls::Label* status_ = nullptr;
    brls::RecyclerFrame* recycler_ = nullptr;
    brls::Box* recyclerHost_ = nullptr;
    HomebrewDataSource* dataSource_ = nullptr;
    bool busy_ = false;
    bool loading_ = false;
    std::shared_ptr<std::atomic<bool>> alive_ =
        std::make_shared<std::atomic<bool>>(true);
};

inline void HomebrewDataSource::didSelectRowAt(brls::RecyclerFrame*,
                                               brls::IndexPath index) {
    owner_->confirmInstall(static_cast<size_t>(index.row));
}

} // namespace gamehubnx::ui
