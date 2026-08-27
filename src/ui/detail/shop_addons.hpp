#pragma once

// Lista de DLC y actualizaciones de un juego de la tienda.
//
// La ficha ya sabia CUANTOS complementos hay --lo decia en una fila de la tabla
// de datos-- pero no habia forma de verlos ni de instalarlos: addOns_ se
// consumia solo como .size(). Esta pantalla los ensena y deja elegir uno.
//
// Lo que hace al elegir no es instalar: es abrir la ficha de ese complemento.
// Un DLC de la tienda es una CatalogEntry como cualquier otra --su URL, su
// tamano, su Title ID-- asi que GameDetailActivity ya sabe describirlo e
// instalarlo. Reusarla evita duplicar el camino de instalacion, que es
// justamente donde vivian los cuatro cortes que hubo que arreglar.

#include <functional>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "app/catalog_service.hpp"
#include "app/gamehub_catalog.hpp"
#include "app/installed_title_service.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/i18n.hpp"
#include "ui/theme.hpp"

namespace pipensx::ui {

class ShopAddOnsActivity;

class ShopAddOnCell : public brls::RecyclerCell {
public:
    ShopAddOnCell() {
        setFocusable(true);
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setPadding(10, 20, 10, 20);
        setHeight(76);

        auto* left = new brls::Box(brls::Axis::COLUMN);
        left->setGrow(1);
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
        left->addView(title_);
        left->addView(meta_);
        addView(left);

        badge_ = new brls::Label();
        badge_->setFontSize(theme::kFontSmall);
        badge_->setMarginLeft(16);
        addView(badge_);
    }

    void setEntry(const CatalogEntry& entry, bool installed) {
        setTextIfChanged(title_, entry.title);
        const bool update = !gamehub::isDlcTitleId(entry.titleId);
        std::string meta = update ? tr("pipensx/addons/kind_update")
                                  : tr("pipensx/addons/kind_dlc");
        if (entry.size)
            meta += "   " + formatBytes(entry.size);
        setTextIfChanged(meta_, meta);
        setTextIfChanged(badge_, installed ? tr("pipensx/addons/installed")
                                           : std::string());
        badge_->setTextColor(theme::success());
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
    brls::Label* title_;
    brls::Label* meta_;
    brls::Label* badge_;
};

class ShopAddOnsDataSource : public brls::RecyclerDataSource {
public:
    ShopAddOnsDataSource(ShopAddOnsActivity* owner,
                         std::vector<CatalogEntry> entries,
                         InstalledTitleService* installed)
        : owner_(owner), entries_(std::move(entries)), installed_(installed) {}

    int numberOfSections(brls::RecyclerFrame*) override { return 1; }

    int numberOfRows(brls::RecyclerFrame*, int) override {
        return static_cast<int>(entries_.size());
    }

    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler,
                                    brls::IndexPath index) override;

    void didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) override;

    const std::vector<CatalogEntry>& entries() const { return entries_; }

private:
    ShopAddOnsActivity* owner_;
    std::vector<CatalogEntry> entries_;
    InstalledTitleService* installed_;
};

class ShopAddOnsActivity : public brls::Activity {
public:
    using PickCallback = std::function<void(const CatalogEntry&)>;

    ShopAddOnsActivity(std::string gameTitle, std::vector<CatalogEntry> entries,
                       InstalledTitleService* installed, PickCallback onPick)
        : onPick_(std::move(onPick)) {
        auto* content = new brls::Box(brls::Axis::COLUMN);
        content->setGrow(1);
        content->setPadding(18, 38, 18, 34);
        content->setBackgroundColor(theme::overlay());
        content->setCornerRadius(12);

        auto* title = new brls::Label();
        title->setFontSize(26);
        title->setText(tr("pipensx/addons/title"));
        content->addView(title);

        auto* subtitle = new brls::Label();
        subtitle->setFontSize(theme::kFontSmall);
        subtitle->setTextColor(theme::textSecondary());
        subtitle->setMarginBottom(10);
        subtitle->setText(tr("pipensx/addons/hint"));
        content->addView(subtitle);

        recycler_ = new brls::RecyclerFrame();
        recycler_->setGrow(1);
        recycler_->setPadding(6, 0, 6, 0);
        recycler_->estimatedRowHeight = 76;
        recycler_->registerCell("AddOn", [] { return new ShopAddOnCell(); });
        dataSource_ =
            new ShopAddOnsDataSource(this, std::move(entries), installed);
        recycler_->setDataSource(dataSource_);
        content->addView(recyclerHost(recycler_));

        frame_ = new brls::AppletFrame(content);
        frame_->setTitle(gameTitle.empty() ? tr("pipensx/addons/title")
                                           : gameTitle);
    }

    brls::View* createContentView() override { return frame_; }

    void onContentAvailable() override {
        registerAction(tr("pipensx/common/close"), brls::BUTTON_B,
                       [](brls::View*) {
                           brls::Application::popActivity();
                           return true;
                       });
    }

    void pick(const CatalogEntry& entry) {
        if (onPick_)
            onPick_(entry);
    }

private:
    PickCallback onPick_;
    brls::AppletFrame* frame_ = nullptr;
    brls::RecyclerFrame* recycler_ = nullptr;
    ShopAddOnsDataSource* dataSource_ = nullptr;
};

inline brls::RecyclerCell* ShopAddOnsDataSource::cellForRow(
    brls::RecyclerFrame* recycler, brls::IndexPath index) {
    auto* cell =
        static_cast<ShopAddOnCell*>(recycler->dequeueReusableCell("AddOn"));
    if (index.row < 0 || static_cast<size_t>(index.row) >= entries_.size())
        return cell;
    const CatalogEntry& entry = entries_[static_cast<size_t>(index.row)];
    // contains()/containsDlc() ya normalizan a mayusculas por dentro, asi que
    // el id va tal cual. Un DLC vive en su propio conjunto: preguntar por el en
    // contains() daria siempre que no.
    const bool dlc = gamehub::isDlcTitleId(entry.titleId);
    const bool installed =
        installed_ && !entry.titleId.empty() &&
        (dlc ? installed_->containsDlc(entry.titleId)
             : installed_->contains(entry.titleId));
    cell->setEntry(entry, installed);
    return cell;
}

inline void ShopAddOnsDataSource::didSelectRowAt(brls::RecyclerFrame*,
                                                  brls::IndexPath index) {
    if (index.row < 0 || static_cast<size_t>(index.row) >= entries_.size())
        return;
    owner_->pick(entries_[static_cast<size_t>(index.row)]);
}

}  // namespace pipensx::ui
