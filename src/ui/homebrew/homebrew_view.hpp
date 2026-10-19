#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <borealis.hpp>

#include "app/homebrew_cache.hpp"
#include "app/homebrew_icons.hpp"
#include "app/homebrew_installed.hpp"
#include "app/homebrew_loader.hpp"
#include "app/homebrew_source.hpp"
#include "ui/common/async_image.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/i18n.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui {

// Una casilla de la cuadricula: una app instalada, o una del catalogo que se
// puede instalar. Una app instalada y con release en el catalogo es la misma
// casilla, con la accion de actualizar si el release es mas nuevo.
struct StoreItem {
    enum class Chip { Install, Installed, Update };
    std::string title;
    std::string sub;          // "v1.2.0 · autor"
    std::string key;          // clave de la cache de icono
    Chip chip = Chip::Install;
    std::string chipText;
    HomebrewIconSource icon;
    int catalogApp = -1;      // indice en la lista del catalogo, o -1
    bool installed = false;
    bool updatable = false;
};

// Directorio de instalacion de un release bajo switch/: la carpeta del .nro.
inline std::string storeDirFor(const HomebrewRelease& release) {
    if (!release.isArchive()) {
        std::string stem = release.nroName;
        if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".nro") == 0)
            stem = stem.substr(0, stem.size() - 4);
        return stem;
    }
    // binaryPath: "/switch/<carpeta>/<archivo>.nro"
    const std::string prefix = "/switch/";
    if (release.binaryPath.compare(0, prefix.size(), prefix) != 0)
        return {};
    const std::string rest = release.binaryPath.substr(prefix.size());
    const size_t slash = rest.find('/');
    return slash == std::string::npos ? std::string() : rest.substr(0, slash);
}

inline std::string storeLower(std::string text) {
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Pixeles ya decodificados de un icono, listos para la textura.
struct StoreIconPixels {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// Una casilla de la cuadricula. Ocupa el ancho de su columna; la portada es un
// rectangulo fijo y el icono se centra dentro.
class StoreTile : public brls::Box {
public:
    StoreTile() : brls::Box(brls::Axis::COLUMN) {
        setFocusable(true);
        setGrow(1.0f);
        setMarginLeft(10.0f);
        setMarginRight(10.0f);

        cover_ = new brls::Box(brls::Axis::COLUMN);
        cover_->setHeight(kCoverHeight);
        // Dentro de una columna, la portada tiene que estirarse en ancho: con
        // width 0 quedaba invisible, y con ella tambien su icono y la inicial.
        cover_->setAlignSelf(brls::AlignSelf::STRETCH);
        cover_->setCornerRadius(theme::kRadiusMedium);
        cover_->setBackgroundColor(theme::surface());
        cover_->setAlignItems(brls::AlignItems::CENTER);
        cover_->setJustifyContent(brls::JustifyContent::CENTER);
        cover_->setClipsToBounds(true);

        placeholder_ = new brls::Label();
        placeholder_->setFontSize(theme::kFontTitle);
        placeholder_->setTextColor(theme::textSecondary());
        cover_->addView(placeholder_);

        image_ = new AsyncRgbaImage();
        image_->setWidth(kIconSize);
        image_->setHeight(kIconSize);
        image_->setScalingType(brls::ImageScalingType::FIT);
        image_->setVisibility(brls::Visibility::GONE);
        cover_->addView(image_);

        chipBox_ = new brls::Box();
        chipBox_->setPositionType(brls::PositionType::ABSOLUTE);
        chipBox_->setPositionBottom(theme::kSpacingUnit);
        chipBox_->setPositionLeft(theme::kSpacingUnit);
        chipBox_->setPaddingLeft(6.0f);
        chipBox_->setPaddingRight(6.0f);
        chipBox_->setCornerRadius(theme::kRadiusSmall);
        chipBox_->setVisibility(brls::Visibility::GONE);
        chip_ = new brls::Label();
        chip_->setFontSize(theme::kFontCaption);
        chipBox_->addView(chip_);
        cover_->addView(chipBox_);
        addView(cover_);

        // Una sola accion de clic: reasignar la casilla solo cambia el callback.
        registerClickAction([this](brls::View*) {
            if (onActivate_)
                onActivate_();
            return true;
        });

        title_ = new brls::Label();
        title_->setAlignSelf(brls::AlignSelf::STRETCH);
        title_->setSingleLine(true);
        title_->setAutoAnimate(false);
        title_->setFontSize(theme::kFontSmall);
        title_->setMarginTop(8.0f);
        addView(title_);

        sub_ = new brls::Label();
        sub_->setAlignSelf(brls::AlignSelf::STRETCH);
        sub_->setSingleLine(true);
        sub_->setAutoAnimate(false);
        sub_->setFontSize(theme::kFontCaption);
        sub_->setMarginTop(2.0f);
        sub_->setTextColor(theme::textTertiary());
        addView(sub_);
    }

    static constexpr float kCoverHeight = 156.0f;
    static constexpr float kIconSize = 120.0f;

    void setItem(const StoreItem& item) {
        setTextIfChanged(title_, item.title);
        setTextIfChanged(sub_, item.sub);
        setTextIfChanged(placeholder_, item.title.empty()
                                           ? std::string("?")
                                           : std::string(1, static_cast<char>(
                                                 std::toupper(static_cast<unsigned char>(item.title[0])))));
        if (item.chipText.empty()) {
            chipBox_->setVisibility(brls::Visibility::GONE);
        } else {
            chipBox_->setVisibility(brls::Visibility::VISIBLE);
            setTextIfChanged(chip_, item.chipText);
            if (item.chip == StoreItem::Chip::Update) {
                chipBox_->setBackgroundColor(theme::accent());
                chip_->setTextColor(theme::onAccent());
            } else {
                chipBox_->setBackgroundColor(theme::overlay());
                chip_->setTextColor(item.chip == StoreItem::Chip::Installed
                                        ? theme::success()
                                        : theme::textPrimary());
            }
        }
    }

    // Icono ya decodificado, o nulo para dejar la inicial.
    void setIcon(const StoreIconPixels* pixels) {
        if (!pixels || pixels->width <= 0) {
            image_->setVisibility(brls::Visibility::GONE);
            placeholder_->setVisibility(brls::Visibility::VISIBLE);
            return;
        }
        placeholder_->setVisibility(brls::Visibility::GONE);
        image_->setVisibility(brls::Visibility::VISIBLE);
        image_->setRgbaNow(pixels->rgba.data(), pixels->width, pixels->height);
    }

    void setOnActivate(std::function<void()> onActivate) {
        onActivate_ = std::move(onActivate);
    }

private:
    brls::Box* cover_ = nullptr;
    AsyncRgbaImage* image_ = nullptr;
    brls::Label* placeholder_ = nullptr;
    brls::Box* chipBox_ = nullptr;
    brls::Label* chip_ = nullptr;
    brls::Label* title_ = nullptr;
    brls::Label* sub_ = nullptr;
    std::function<void()> onActivate_;
};

// Una fila de la cuadricula: hasta cinco casillas.
class StoreRowCell : public brls::RecyclerCell {
public:
    StoreRowCell() {
        setFocusable(false);
        setAxis(brls::Axis::ROW);
        setAlignSelf(brls::AlignSelf::STRETCH);
        setHeight(rowHeight());
        setLineBottom(0);
        setLineColor(brls::TRANSPARENT);
        for (int i = 0; i < kColumns; ++i) {
            tiles_[i] = new StoreTile();
            addView(tiles_[i]);
        }
    }

    static constexpr int kColumns = 5;
    static float rowHeight() { return StoreTile::kCoverHeight + 76.0f; }

    // `items` es la lista visible; la fila muestra [first, first+kColumns).
    void setRow(const std::vector<StoreItem>& items, size_t first,
                const std::vector<const StoreIconPixels*>& icons,
                std::function<void(size_t)> onActivate) {
        for (int i = 0; i < kColumns; ++i) {
            const size_t index = first + static_cast<size_t>(i);
            if (index < items.size()) {
                tiles_[i]->setVisibility(brls::Visibility::VISIBLE);
                tiles_[i]->setFocusable(true);
                tiles_[i]->setItem(items[index]);
                tiles_[i]->setIcon(index < icons.size() ? icons[index] : nullptr);
                tiles_[i]->setOnActivate([onActivate, index] { onActivate(index); });
            } else {
                tiles_[i]->setVisibility(brls::Visibility::INVISIBLE);
                tiles_[i]->setFocusable(false);
            }
        }
    }

private:
    StoreTile* tiles_[kColumns] = {};
};

class HomebrewView;

class StoreDataSource : public brls::RecyclerDataSource {
public:
    explicit StoreDataSource(HomebrewView* owner) : owner_(owner) {}
    int numberOfRows(brls::RecyclerFrame*, int) override;
    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler,
                                   brls::IndexPath index) override;
    float heightForRow(brls::RecyclerFrame*, brls::IndexPath) override {
        return StoreRowCell::rowHeight();
    }

private:
    HomebrewView* owner_;
};

// Apps Store: cuadricula de apps con icono, dos vistas (instaladas y
// disponibles), buscador y actualizacion de lo que tenga version nueva.
class HomebrewView : public brls::Box {
public:
    // autoLoad = false: no toca la red; el catalogo lo da loadCatalogForTesting.
    explicit HomebrewView(bool autoLoad = true);
    ~HomebrewView() override;

    // Para el harness golden: catalogo fijo y la pestana que se muestra.
    void loadCatalogForTesting(std::vector<HomebrewApp> apps, bool available) {
        catalog_ = std::move(apps);
        tab_ = available ? Tab::Available : Tab::Installed;
        rebuild();
    }

    // Lo que la cuadricula necesita para pintar la fila `row`.
    std::vector<StoreItem>& visibleItems() { return visible_; }
    std::vector<const StoreIconPixels*> iconsFor(size_t first) const;
    void activate(size_t index);

private:
    enum class Tab { Installed, Available };

    void load(bool force);
    void rebuild();
    void applyFilter();
    void setTab(Tab tab);
    void openSearch();
    void confirmInstall(size_t catalogApp);
    void installApp(size_t catalogApp, bool fromUpdateAll);
    void updateAll();
    void pumpUpdateAll();
    void refreshVisible();
    void requestIcon(const StoreItem& item);
    void iconWorker();
    void updateTabLabels();

    std::vector<HomebrewApp> catalog_;
    std::vector<InstalledNro> installed_;
    std::vector<StoreItem> installedItems_;
    std::vector<StoreItem> availableItems_;
    std::vector<StoreItem> visible_;
    Tab tab_ = Tab::Installed;
    std::string query_;
    bool loading_ = false;
    bool busy_ = false;
    std::vector<size_t> updateQueue_;

    // Iconos: el hilo de trabajo baja y decodifica; la UI solo pinta lo listo.
    std::map<std::string, StoreIconPixels> icons_;
    std::set<std::string> iconsPending_;
    std::deque<HomebrewIconSource> iconQueue_;
    std::mutex iconMutex_;
    std::condition_variable iconCv_;
    std::atomic<bool> stopIcons_{false};
    std::thread iconThread_;

    brls::Button* tabInstalled_ = nullptr;
    brls::Button* tabAvailable_ = nullptr;
    brls::Label* status_ = nullptr;
    brls::RecyclerFrame* recycler_ = nullptr;
    brls::Box* recyclerHost_ = nullptr;
    StoreDataSource* dataSource_ = nullptr;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    friend class StoreDataSource;
};

inline HomebrewView::HomebrewView(bool autoLoad) : brls::Box(brls::Axis::COLUMN) {
    setGrow(1);
    setPadding(16, 32, 0, 32);

    auto* toolbar = new brls::Box(brls::Axis::ROW);
    toolbar->setAlignItems(brls::AlignItems::CENTER);
    tabInstalled_ = new brls::Button();
    tabInstalled_->setStyle(&brls::BUTTONSTYLE_DEFAULT);
    tabInstalled_->setShrink(0.0f);
    tabInstalled_->registerClickAction([this](brls::View*) {
        setTab(Tab::Installed);
        return true;
    });
    toolbar->addView(tabInstalled_);
    tabAvailable_ = new brls::Button();
    tabAvailable_->setStyle(&brls::BUTTONSTYLE_DEFAULT);
    tabAvailable_->setShrink(0.0f);
    tabAvailable_->setMarginLeft(8.0f);
    tabAvailable_->registerClickAction([this](brls::View*) {
        setTab(Tab::Available);
        return true;
    });
    toolbar->addView(tabAvailable_);
    auto* spacer = new brls::Box();
    spacer->setGrow(1.0f);
    toolbar->addView(spacer);
    addView(toolbar);

    status_ = new brls::Label();
    status_->setFontSize(theme::kFontSmall);
    status_->setTextColor(theme::textTertiary());
    status_->setMarginTop(12.0f);
    status_->setMarginBottom(12.0f);
    addView(status_);

    recycler_ = new brls::RecyclerFrame();
    recycler_->setGrow(1);
    recycler_->estimatedRowHeight = StoreRowCell::rowHeight();
    recycler_->registerCell("StoreRow", [] { return new StoreRowCell(); });
    dataSource_ = new StoreDataSource(this);
    recycler_->setDataSource(dataSource_);
    recyclerHost_ = recyclerHost(recycler_);
    recyclerHost_->setVisibility(brls::Visibility::GONE);
    addView(recyclerHost_);

    registerAction(tr("gamehubnx/homebrew/search"), brls::BUTTON_X, [this](brls::View*) {
        openSearch();
        return true;
    });
    registerAction(tr("gamehubnx/homebrew/update_all"), brls::BUTTON_Y, [this](brls::View*) {
        updateAll();
        return true;
    });
    registerAction(tr("gamehubnx/homebrew/refresh"), brls::BUTTON_LB, [this](brls::View*) {
        load(true);
        return true;
    });

    iconThread_ = std::thread([this] { iconWorker(); });
    updateTabLabels();
    if (autoLoad)
        load(false);
}

inline HomebrewView::~HomebrewView() {
    alive_->store(false);
    {
        std::lock_guard<std::mutex> lock(iconMutex_);
        stopIcons_.store(true);
    }
    iconCv_.notify_all();
    if (iconThread_.joinable())
        iconThread_.join();
}

inline int StoreDataSource::numberOfRows(brls::RecyclerFrame*, int) {
    const size_t n = owner_->visibleItems().size();
    return static_cast<int>((n + StoreRowCell::kColumns - 1) / StoreRowCell::kColumns);
}

inline brls::RecyclerCell* StoreDataSource::cellForRow(brls::RecyclerFrame* recycler,
                                                       brls::IndexPath index) {
    auto* cell = static_cast<StoreRowCell*>(recycler->dequeueReusableCell("StoreRow"));
    const size_t first = static_cast<size_t>(index.row) * StoreRowCell::kColumns;
    HomebrewView* owner = owner_;
    cell->setRow(owner->visibleItems(), first, owner->iconsFor(first),
                 [owner](size_t i) { owner->activate(i); });
    return cell;
}

inline std::vector<const StoreIconPixels*> HomebrewView::iconsFor(size_t first) const {
    std::vector<const StoreIconPixels*> out(StoreRowCell::kColumns, nullptr);
    for (int i = 0; i < StoreRowCell::kColumns; ++i) {
        const size_t index = first + static_cast<size_t>(i);
        if (index >= visible_.size())
            break;
        auto it = icons_.find(visible_[index].key);
        if (it != icons_.end())
            out[i] = &it->second;
    }
    return out;
}

inline void HomebrewView::activate(size_t index) {
    if (index >= visible_.size() || busy_)
        return;
    const StoreItem& item = visible_[index];
    if (item.catalogApp < 0) {
        brls::Application::notify(tr("gamehubnx/homebrew/not_in_catalog"));
        return;
    }
    confirmInstall(static_cast<size_t>(item.catalogApp));
}

inline void HomebrewView::updateTabLabels() {
    const std::string installed = tr("gamehubnx/homebrew/installed_tab") + "  " +
                                  std::to_string(installedItems_.size());
    const std::string available = tr("gamehubnx/homebrew/available_tab") + "  " +
                                  std::to_string(availableItems_.size());
    setTextIfChanged(tabInstalled_, installed);
    setTextIfChanged(tabAvailable_, available);
}

inline void HomebrewView::setTab(Tab tab) {
    tab_ = tab;
    applyFilter();
}

inline void HomebrewView::openSearch() {
    if (busy_)
        return;
    brls::Application::getImeManager()->openForText(
        [this](std::string text) {
            query_ = std::move(text);
            applyFilter();
        },
        tr("gamehubnx/homebrew/search_placeholder"), "", 128, query_,
        brls::KEYBOARD_DISABLE_NONE);
}

inline void HomebrewView::applyFilter() {
    const std::vector<StoreItem>& source =
        tab_ == Tab::Installed ? installedItems_ : availableItems_;
    const std::string needle = storeLower(query_);
    visible_.clear();
    for (const StoreItem& item : source) {
        if (needle.empty() || storeLower(item.title).find(needle) != std::string::npos ||
            storeLower(item.sub).find(needle) != std::string::npos)
            visible_.push_back(item);
    }
    updateTabLabels();
    if (visible_.empty()) {
        status_->setText(tr("gamehubnx/homebrew/empty_view"));
        recyclerHost_->setVisibility(brls::Visibility::GONE);
    } else {
        status_->setText(query_.empty()
            ? tr("gamehubnx/homebrew/sources")
            : tr("gamehubnx/homebrew/search_result", std::to_string(visible_.size())));
        recyclerHost_->setVisibility(brls::Visibility::VISIBLE);
    }
    recycler_->reloadData();
    // Pide los iconos de lo visible; el resto llega cuando se desplace.
    for (size_t i = 0; i < visible_.size() && i < 25; ++i)
        requestIcon(visible_[i]);
}

inline void HomebrewView::requestIcon(const StoreItem& item) {
    if (icons_.count(item.key) || iconsPending_.count(item.key))
        return;
    iconsPending_.insert(item.key);
    {
        std::lock_guard<std::mutex> lock(iconMutex_);
        iconQueue_.push_back(item.icon);
    }
    iconCv_.notify_one();
}

// Hilo de trabajo: baja (o lee) el icono, lo decodifica y lo entrega a la UI.
inline void HomebrewView::iconWorker() {
    while (true) {
        HomebrewIconSource source;
        {
            std::unique_lock<std::mutex> lock(iconMutex_);
            iconCv_.wait(lock, [this] { return stopIcons_.load() || !iconQueue_.empty(); });
            if (stopIcons_.load())
                return;
            source = std::move(iconQueue_.front());
            iconQueue_.pop_front();
        }
        StoreIconPixels pixels;
        std::vector<uint8_t> bytes;
        std::string error;
        if (loadHomebrewIconBytes(source, bytes, error) &&
            decodeHomebrewIcon(bytes, pixels.rgba, pixels.width, pixels.height)) {
            // Se entrega aunque luego no se pinte: la UI decide.
        } else {
            pixels = StoreIconPixels();
        }
        auto alive = alive_;
        const std::string key = source.cacheKey;
        brls::sync([this, alive, key, pixels = std::move(pixels)]() mutable {
            if (!alive->load())
                return;
            iconsPending_.erase(key);
            if (pixels.width > 0)
                icons_[key] = std::move(pixels);
            else
                icons_[key] = StoreIconPixels();  // sin icono: no se vuelve a pedir
            refreshVisible();
        });
    }
}

inline void HomebrewView::refreshVisible() {
    recycler_->reloadData();
}

// Carga el catalogo en un hilo de trabajo y reconstruye las dos vistas.
inline void HomebrewView::load(bool force) {
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
            catalog_ = std::move(result.apps);
            if (catalog_.empty() && !result.error.empty()) {
                status_->setText(result.error);
            }
            rebuild();
            if (result.staleAgeSec >= 0) {
                status_->setText(tr("gamehubnx/homebrew/offline",
                                    std::to_string(result.staleAgeSec / 3600)));
            }
        });
    });
}

// Cruza lo instalado con el catalogo. Una app instalada con release en el
// catalogo es la misma casilla, con su accion de actualizar. Lo instalado sin
// release se lista igual, sin accion, y los releases sin instalar van a
// Disponibles.
inline void HomebrewView::rebuild() {
    installed_ = scanInstalledNros("sdmc:/switch");
    std::vector<bool> used(installed_.size(), false);
    installedItems_.clear();
    availableItems_.clear();

    for (size_t a = 0; a < catalog_.size(); ++a) {
        const HomebrewApp& app = catalog_[a];
        const HomebrewRelease* rel = app.recommended();
        if (!rel)
            continue;
        StoreItem item;
        item.title = app.displayName();
        item.catalogApp = static_cast<int>(a);
        item.key = rel->isArchive() ? "hb-" + app.repo : homebrewCacheKeyForGithub(app.repo);
        item.icon.cacheKey = item.key;
        if (rel->isArchive())
            item.icon.iconUrl = homebrewSwitchRepoUrl() + "/packages/" + app.repo + "/icon.png";
        else
            item.icon.remoteNro = rel->nroUrl;

        const InstalledNro* hit = findInstalledNro(installed_, storeDirFor(*rel), rel->nroName);
        const size_t found = hit ? static_cast<size_t>(hit - installed_.data())
                                 : installed_.size();
        if (found < installed_.size() && !used[found]) {
            used[found] = true;
            const InstalledNro& nro = installed_[found];
            item.installed = true;
            item.icon.localNro = nro.path;
            item.sub = (nro.version.empty() ? std::string("v?") : "v" + nro.version) +
                       " · " + (nro.author.empty() ? app.author : nro.author);
            if (!nro.version.empty() && compareHomebrewVersions(nro.version, rel->tag) < 0) {
                item.chip = StoreItem::Chip::Update;
                item.chipText = tr("gamehubnx/homebrew/update", rel->tag);
                item.updatable = true;
            } else {
                item.chip = StoreItem::Chip::Installed;
                item.chipText = nro.version.empty()
                    ? tr("gamehubnx/homebrew/no_version")
                    : tr("gamehubnx/homebrew/installed", nro.version);
            }
            installedItems_.push_back(item);
        } else {
            item.sub = rel->tag + " · " + app.author;
            item.chip = StoreItem::Chip::Install;
            item.chipText = tr("gamehubnx/homebrew/install");
            availableItems_.push_back(item);
        }
    }

    // Instalado sin release en el catalogo: se muestra, sin accion.
    for (size_t i = 0; i < installed_.size(); ++i) {
        if (used[i])
            continue;
        const InstalledNro& nro = installed_[i];
        StoreItem item;
        item.title = nro.name;
        item.sub = (nro.version.empty() ? std::string("v?") : "v" + nro.version) +
                   (nro.author.empty() ? std::string() : " · " + nro.author);
        item.key = "local-" + nro.path;
        item.icon.cacheKey = item.key;
        item.icon.localNro = nro.path;
        item.installed = true;
        item.chip = StoreItem::Chip::Installed;
        item.chipText = nro.version.empty() ? tr("gamehubnx/homebrew/no_version")
                                            : tr("gamehubnx/homebrew/installed", nro.version);
        installedItems_.push_back(item);
    }

    std::sort(installedItems_.begin(), installedItems_.end(),
              [](const StoreItem& a, const StoreItem& b) {
                  return storeLower(a.title) < storeLower(b.title);
              });
    std::sort(availableItems_.begin(), availableItems_.end(),
              [](const StoreItem& a, const StoreItem& b) {
                  return storeLower(a.title) < storeLower(b.title);
              });
    applyFilter();
}

// Confirmar antes de instalar o actualizar: escribir en la SD sin preguntar no.
inline void HomebrewView::confirmInstall(size_t catalogApp) {
    if (catalogApp >= catalog_.size() || busy_)
        return;
    const HomebrewApp& app = catalog_[catalogApp];
    const HomebrewRelease* release = app.recommended();
    if (!release)
        return;
    auto* dialog = new brls::Dialog(tr("gamehubnx/homebrew/install_confirm",
                                       app.displayName(), release->tag));
    dialog->addButton(tr("gamehubnx/homebrew/install"),
                      [this, catalogApp] { installApp(catalogApp, false); });
    dialog->addButton(tr("gamehubnx/common/later"), [] {});
    dialog->open();
}

// Instala (o actualiza) un release en un hilo de trabajo y recalcula las vistas.
inline void HomebrewView::installApp(size_t catalogApp, bool fromUpdateAll) {
    if (catalogApp >= catalog_.size() || busy_)
        return;
    const HomebrewRelease release = *catalog_[catalogApp].recommended();
    const std::string title = catalog_[catalogApp].displayName();
    busy_ = true;
    status_->setText(tr("gamehubnx/homebrew/downloading", title));
    auto alive = alive_;
    brls::async([this, alive, release, title, fromUpdateAll] {
        std::string installedPath;
        std::string error;
        const bool ok = installHomebrewRelease(release, installedPath, error);
        brls::sync([this, alive, ok, error, title, fromUpdateAll] {
            if (!alive->load())
                return;
            busy_ = false;
            if (ok) {
                brls::Application::notify(tr("gamehubnx/homebrew/done", title));
            } else {
                brls::Application::notify(tr("gamehubnx/homebrew/failed", error));
            }
            rebuild();
            if (fromUpdateAll)
                pumpUpdateAll();
        });
    });
}

// "Actualizar todo": las apps de la vista Instaladas con version nueva, una tras
// otra, para no saturar la conexion ni la SD.
inline void HomebrewView::updateAll() {
    if (busy_)
        return;
    updateQueue_.clear();
    for (const StoreItem& item : installedItems_)
        if (item.updatable && item.catalogApp >= 0)
            updateQueue_.push_back(static_cast<size_t>(item.catalogApp));
    if (updateQueue_.empty()) {
        brls::Application::notify(tr("gamehubnx/homebrew/nothing_to_update"));
        return;
    }
    auto* dialog = new brls::Dialog(tr("gamehubnx/homebrew/update_all_confirm",
                                       std::to_string(updateQueue_.size())));
    dialog->addButton(tr("gamehubnx/homebrew/update_all"), [this] { pumpUpdateAll(); });
    dialog->addButton(tr("gamehubnx/common/later"), [this] { updateQueue_.clear(); });
    dialog->open();
}

inline void HomebrewView::pumpUpdateAll() {
    if (busy_ || updateQueue_.empty())
        return;
    const size_t next = updateQueue_.front();
    updateQueue_.erase(updateQueue_.begin());
    installApp(next, true);
}

} // namespace gamehubnx::ui
