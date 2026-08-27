#pragma once

// "Qué instalar": el juego, sus actualizaciones y sus DLC, en una sola lista.
//
// Antes eran dos pantallas que no se hablaban: una lista de complementos que
// solo dejaba abrir uno, y el selector de ficheros heredado del camino de
// torrent, que enseñaba un unico fichero. Para poner al dia un juego habia que
// entrar y salir una vez por cada DLC.
//
// Aqui van los tres en secciones separadas, con marca de lo que ya esta puesto
// en la consola, y con seleccion multiple: se marca lo que se quiera --uno,
// varios o todo-- y se instala de una tacada. La instalacion no se reimplementa:
// se le pasa la seleccion a BatchInstallActivity, que ya sabe hacerlo con
// GameHubProvider desde que la tienda entro por esa ruta.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include <borealis.hpp>

#include "app/app_settings.hpp"
#include "app/catalog_service.hpp"
#include "app/download_manager.hpp"
#include "app/gamehub_catalog.hpp"
#include "app/installed_title_service.hpp"
#include "ui/catalog/batch_install.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/i18n.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui {

/// Que clase de contenido es una fila. Decide en que seccion cae y con que
/// palabra se describe.
enum class ShopItemKind { Game, Update, Dlc };

struct ShopItem {
    CatalogEntry entry;
    ShopItemKind kind = ShopItemKind::Dlc;
    /// Cierto cuando la consola ya lo tiene. En un DLC y en un juego es una
    /// pregunta directa al sistema; en una actualizacion solo se sabe si se
    /// pueden comparar las dos versiones (ver installedUpdate()).
    bool installed = false;
    bool selected = false;
};

class ShopAddOnsActivity;

// ── Version de una actualizacion ─────────────────────────────────────────────
// Los ficheros de actualizacion de esta tienda llevan la version entre
// corchetes: "Juego [0100...800][v2162688].nsp". Se compara con la que reporta
// ncm para el titulo instalado. Cuando falta cualquiera de las dos NO se marca
// nada: decir "instalada" sin poder compararlo seria peor que no decir nada.
inline uint64_t shopVersionFromName(const std::string& name) {
    for (size_t i = 0; i + 2 < name.size(); ++i) {
        if (name[i] != '[' || (name[i + 1] != 'v' && name[i + 1] != 'V'))
            continue;
        size_t j = i + 2;
        uint64_t value = 0;
        bool any = false;
        while (j < name.size() && name[j] >= '0' && name[j] <= '9') {
            value = value * 10 + static_cast<uint64_t>(name[j] - '0');
            any = true;
            ++j;
        }
        if (any && j < name.size() && name[j] == ']')
            return value;
    }
    return 0;
}

class ShopItemCell : public brls::RecyclerCell {
public:
    ShopItemCell() {
        setFocusable(true);
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setPadding(10, 20, 10, 20);
        setHeight(76);

        mark_ = new brls::Label();
        mark_->setFontSize(theme::kFontBody);
        mark_->setWidth(34);
        addView(mark_);

        auto* text = new brls::Box(brls::Axis::COLUMN);
        text->setGrow(1);
        title_ = new brls::Label();
        title_->setSingleLine(true);
        title_->setAutoAnimate(false);
        title_->setFontSize(theme::kFontBody);
        meta_ = new brls::Label();
        meta_->setSingleLine(true);
        meta_->setAutoAnimate(false);
        meta_->setFontSize(theme::kFontSmall);
        meta_->setTextColor(theme::textSecondary());
        meta_->setMarginTop(4);
        text->addView(title_);
        text->addView(meta_);
        addView(text);

        badge_ = new brls::Label();
        badge_->setFontSize(theme::kFontSmall);
        badge_->setMarginLeft(16);
        badge_->setTextColor(theme::success());
        addView(badge_);
    }

    size_t index() const { return index_; }

    void setItem(const ShopItem& item, size_t index) {
        index_ = index;
        // Lo ya instalado no se puede marcar: seleccionarlo solo serviria para
        // volver a bajar lo que ya esta.
        setTextIfChanged(mark_, item.installed ? std::string()
                                               : (item.selected ? "[x]" : "[ ]"));
        mark_->setTextColor(item.selected ? theme::accent()
                                          : theme::textSecondary());
        setTextIfChanged(title_, item.entry.title);
        std::string meta = kindLabel(item.kind);
        if (item.entry.size)
            meta += "   " + formatBytes(item.entry.size);
        setTextIfChanged(meta_, meta);
        setTextIfChanged(badge_, item.installed
                                     ? tr("gamehubnx/addons/installed")
                                     : std::string());
    }

    static std::string kindLabel(ShopItemKind kind) {
        switch (kind) {
        case ShopItemKind::Game:   return tr("gamehubnx/addons/kind_game");
        case ShopItemKind::Update: return tr("gamehubnx/addons/kind_update");
        case ShopItemKind::Dlc:    return tr("gamehubnx/addons/kind_dlc");
        }
        return {};
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
    size_t index_ = 0;
    brls::Label* mark_;
    brls::Label* title_;
    brls::Label* meta_;
    brls::Label* badge_;
};

class ShopAddOnsDataSource : public brls::RecyclerDataSource {
public:
    struct Section {
        std::string title;
        std::vector<size_t> rows;   // indices en items_
    };

    explicit ShopAddOnsDataSource(ShopAddOnsActivity* owner) : owner_(owner) {}

    void setItems(std::vector<ShopItem> items) {
        items_ = std::move(items);
        rebuild();
    }

    void rebuild() {
        sections_.clear();
        const struct { ShopItemKind kind; const char* key; } grupos[] = {
            {ShopItemKind::Game,   "gamehubnx/addons/section_game"},
            {ShopItemKind::Update, "gamehubnx/addons/section_updates"},
            {ShopItemKind::Dlc,    "gamehubnx/addons/section_dlc"},
        };
        for (const auto& g : grupos) {
            Section section;
            section.title = tr(g.key);
            for (size_t i = 0; i < items_.size(); ++i)
                if (items_[i].kind == g.kind)
                    section.rows.push_back(i);
            if (!section.rows.empty())
                sections_.push_back(std::move(section));
        }
        // Nunca cero secciones: un data source vacio hace que selectRowAt de
        // borealis indexe un vector sin elementos. Ver downloads_view.hpp.
        if (sections_.empty())
            sections_.push_back({tr("gamehubnx/addons/section_dlc"), {}});
    }

    std::vector<ShopItem>& items() { return items_; }
    const std::vector<ShopItem>& items() const { return items_; }

    /// Indice en items_ de una fila pintada, o SIZE_MAX si no hay.
    size_t itemIndexAt(brls::IndexPath index) const {
        if (index.section < 0 ||
            static_cast<size_t>(index.section) >= sections_.size())
            return SIZE_MAX;
        const Section& section = sections_[static_cast<size_t>(index.section)];
        if (index.row < 0 ||
            static_cast<size_t>(index.row) >= section.rows.size())
            return SIZE_MAX;
        return section.rows[static_cast<size_t>(index.row)];
    }

    int numberOfSections(brls::RecyclerFrame*) override {
        return static_cast<int>(sections_.size());
    }

    int numberOfRows(brls::RecyclerFrame*, int section) override {
        if (section < 0 || static_cast<size_t>(section) >= sections_.size())
            return 0;
        const size_t n = sections_[static_cast<size_t>(section)].rows.size();
        return n == 0 ? 1 : static_cast<int>(n);
    }

    std::string titleForHeader(brls::RecyclerFrame*, int section) override {
        if (section < 0 || static_cast<size_t>(section) >= sections_.size())
            return {};
        return sections_[static_cast<size_t>(section)].title;
    }

    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler,
                                    brls::IndexPath index) override;
    void didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) override;

private:
    ShopAddOnsActivity* owner_;
    std::vector<ShopItem> items_;
    std::vector<Section> sections_;
};

class ShopAddOnsActivity : public brls::Activity {
public:
    ShopAddOnsActivity(std::string gameTitle, std::vector<ShopItem> items,
                       DownloadManager* manager, AppSettings* settings,
                       std::function<void()> viewDownloads = nullptr)
        : manager_(manager), settings_(settings),
          viewDownloads_(std::move(viewDownloads)) {
        auto* content = new brls::Box(brls::Axis::COLUMN);
        content->setGrow(1);
        content->setPadding(18, 38, 18, 34);
        content->setBackgroundColor(theme::overlay());
        content->setCornerRadius(12);

        auto* title = new brls::Label();
        title->setFontSize(26);
        title->setText(tr("gamehubnx/addons/title"));
        content->addView(title);

        summary_ = new brls::Label();
        summary_->setFontSize(theme::kFontSmall);
        summary_->setTextColor(theme::textSecondary());
        summary_->setMarginBottom(10);
        content->addView(summary_);

        recycler_ = new brls::RecyclerFrame();
        recycler_->setGrow(1);
        recycler_->setPadding(6, 0, 6, 0);
        recycler_->estimatedRowHeight = 76;
        recycler_->registerCell("Item", [] { return new ShopItemCell(); });
        dataSource_ = new ShopAddOnsDataSource(this);
        // Sembrado ANTES de entregarlo: ver la nota del mismo patron en
        // downloads_view.hpp, que costo un crash.
        dataSource_->setItems(std::move(items));
        recycler_->setDataSource(dataSource_);
        content->addView(recyclerHost(recycler_));

        auto* buttons = new brls::Box(brls::Axis::ROW);
        buttons->setMarginTop(12);
        install_ = addButton(buttons, &brls::BUTTONSTYLE_PRIMARY,
                             [this] { installSelected(); });
        addButton(buttons, &brls::BUTTONSTYLE_DEFAULT,
                  [this] { selectAll(true); })
            ->setText(tr("gamehubnx/addons/select_all"));
        addButton(buttons, &brls::BUTTONSTYLE_DEFAULT,
                  [this] { selectAll(false); })
            ->setText(tr("gamehubnx/addons/select_none"));
        content->addView(buttons);

        frame_ = new brls::AppletFrame(content);
        frame_->setTitle(gameTitle.empty() ? tr("gamehubnx/addons/title")
                                           : gameTitle);
        refreshSummary();
    }

    brls::View* createContentView() override { return frame_; }

    void onContentAvailable() override {
        registerAction(tr("gamehubnx/common/close"), brls::BUTTON_B,
                       [](brls::View*) {
                           brls::Application::popActivity();
                           return true;
                       });
    }

    /// Alterna la seleccion de una fila. Lo instalado no se puede marcar.
    void toggle(size_t index) {
        auto& items = dataSource_->items();
        if (index >= items.size() || items[index].installed)
            return;
        items[index].selected = !items[index].selected;
        repaintVisible();
        refreshSummary();
    }

private:
    brls::Button* addButton(brls::Box* row, const brls::ButtonStyle* style,
                            std::function<void()> action) {
        auto* button = new brls::Button();
        button->setStyle(style);
        button->setFontSize(18);
        button->setHeight(46);
        button->setGrow(1);
        if (row->getChildren().size())
            button->setMarginLeft(12);
        button->registerClickAction([action](brls::View*) {
            action();
            return true;
        });
        row->addView(button);
        return button;
    }

    void selectAll(bool selected) {
        for (ShopItem& item : dataSource_->items())
            if (!item.installed)
                item.selected = selected;
        repaintVisible();
        refreshSummary();
    }

    // Repinta solo lo que hay en pantalla. reloadData() recicla todas las
    // celdas, incluida la enfocada, y Borealis se queda con currentFocus
    // apuntando a memoria liberada --giveFocus(nullptr) no limpia nada--, que
    // es lo que dibuja el recuadro de seleccion fuera de sitio. Repintando en
    // sitio la celda enfocada sigue viva y ademas el scroll no salta.
    void repaintVisible() {
        const auto& items = dataSource_->items();
        for (ShopItemCell* cell : visibleCells<ShopItemCell>(recycler_)) {
            const size_t i = cell->index();
            if (i < items.size())
                cell->setItem(items[i], i);
        }
    }

    std::vector<CatalogEntry> selectedEntries() const {
        std::vector<CatalogEntry> out;
        for (const ShopItem& item : dataSource_->items())
            if (item.selected && !item.installed)
                out.push_back(item.entry);
        return out;
    }

    void refreshSummary() {
        uint64_t bytes = 0;
        int count = 0;
        for (const ShopItem& item : dataSource_->items()) {
            if (!item.selected || item.installed)
                continue;
            ++count;
            bytes += item.entry.size;
        }
        setTextIfChanged(summary_, count == 0
            ? tr("gamehubnx/addons/hint")
            : tr("gamehubnx/addons/selected", count, formatBytes(bytes)));
        setTextIfChanged(install_, count == 0
            ? tr("gamehubnx/common/install")
            : tr("gamehubnx/addons/install_n", count));
        install_->setState(count == 0 ? brls::ButtonState::DISABLED
                                      : brls::ButtonState::ENABLED);
    }

    void installSelected() {
        std::vector<CatalogEntry> entries = selectedEntries();
        if (entries.empty())
            return;
        // Ni una linea de instalacion propia: BatchInstallActivity ya sabe
        // hacerlo, y desde que la tienda entro por la ruta de transferencia usa
        // GameHubProvider cuando toda la seleccion es suya.
        const StreamSelection selection = settings_
            ? settings_->get().streamSelection
            : StreamSelection::AllFiles;
        brls::Application::pushActivity(new BatchInstallActivity(
            manager_, settings_, std::move(entries), selection,
            [](const std::unordered_set<std::string>&) {}, viewDownloads_));
    }

    DownloadManager* manager_;
    AppSettings* settings_;
    std::function<void()> viewDownloads_;
    brls::AppletFrame* frame_ = nullptr;
    brls::RecyclerFrame* recycler_ = nullptr;
    ShopAddOnsDataSource* dataSource_ = nullptr;
    brls::Label* summary_ = nullptr;
    brls::Button* install_ = nullptr;
};

inline brls::RecyclerCell* ShopAddOnsDataSource::cellForRow(
    brls::RecyclerFrame* recycler, brls::IndexPath index) {
    auto* cell =
        static_cast<ShopItemCell*>(recycler->dequeueReusableCell("Item"));
    const size_t i = itemIndexAt(index);
    if (i != SIZE_MAX)
        cell->setItem(items_[i], i);
    return cell;
}

inline void ShopAddOnsDataSource::didSelectRowAt(brls::RecyclerFrame*,
                                                  brls::IndexPath index) {
    const size_t i = itemIndexAt(index);
    if (i != SIZE_MAX)
        owner_->toggle(i);
}

}  // namespace gamehubnx::ui
