#pragma once

#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "app/game_metadata_service.hpp"
#include "ui/catalog/catalog_grid.hpp"
#include "ui/common/async_image.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/i18n.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui {

// Tarjeta de una ROM de plataforma. Tamaño FIJO para todas las consolas: la
// portada tiene el mismo hueco (ancho de columna por kCoverHeight) y la
// caratula se centra dentro sin recortarse. Antes la altura de la fila se
// adaptaba a la primera portada de cada consola; una vertical montaba sobre
// las de al lado y tapaba el titulo.
class RomTile : public brls::Box {
public:
    using Activate = std::function<void(int)>;
    using Focused = std::function<void()>;

    static constexpr float kCoverHeight = 176.0f;

    RomTile() : brls::Box(brls::Axis::COLUMN) {
        setFocusable(true);
        setGrow(1.0f);
        setMarginLeft(10.0f);
        setMarginRight(10.0f);

        cover_ = new brls::Box(brls::Axis::COLUMN);
        cover_->setAlignSelf(brls::AlignSelf::STRETCH);
        cover_->setHeight(kCoverHeight);
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
        image_->setAlignSelf(brls::AlignSelf::STRETCH);
        image_->setHeight(kCoverHeight);
        image_->setScalingType(brls::ImageScalingType::FIT);
        image_->setVisibility(brls::Visibility::GONE);
        cover_->addView(image_);

        modBox_ = new brls::Box();
        modBox_->setPositionType(brls::PositionType::ABSOLUTE);
        modBox_->setPositionTop(theme::kSpacingUnit);
        modBox_->setPositionLeft(theme::kSpacingUnit);
        modBox_->setPaddingLeft(6.0f);
        modBox_->setPaddingRight(6.0f);
        modBox_->setCornerRadius(theme::kRadiusSmall);
        modBox_->setBackgroundColor(theme::accent());
        mod_ = new brls::Label();
        mod_->setFontSize(theme::kFontCaption);
        mod_->setTextColor(theme::onAccent());
        mod_->setText("MOD");
        modBox_->addView(mod_);
        modBox_->setVisibility(brls::Visibility::GONE);
        cover_->addView(modBox_);

        favoriteBox_ = new brls::Box();
        favoriteBox_->setPositionType(brls::PositionType::ABSOLUTE);
        favoriteBox_->setPositionBottom(theme::kSpacingUnit);
        favoriteBox_->setPositionRight(theme::kSpacingUnit);
        favoriteBox_->setWidth(28.0f);
        favoriteBox_->setHeight(28.0f);
        favoriteBox_->setCornerRadius(theme::kRadiusSmall);
        favoriteBox_->setBackgroundColor(theme::overlay());
        favoriteBox_->setAlignItems(brls::AlignItems::CENTER);
        favoriteBox_->setJustifyContent(brls::JustifyContent::CENTER);
        favorite_ = new brls::Label();
        favorite_->setFontSize(theme::kFontCaption);
        favorite_->setTextColor(theme::accent());
        favorite_->setText("★");
        favoriteBox_->addView(favorite_);
        favoriteBox_->setVisibility(brls::Visibility::GONE);
        cover_->addView(favoriteBox_);
        addView(cover_);

        name_ = new brls::Label();
        name_->setAlignSelf(brls::AlignSelf::STRETCH);
        name_->setSingleLine(true);
        name_->setAutoAnimate(false);
        name_->setFontSize(theme::kFontSmall);
        name_->setMarginTop(8.0f);
        addView(name_);

        sub_ = new brls::Label();
        sub_->setAlignSelf(brls::AlignSelf::STRETCH);
        sub_->setSingleLine(true);
        sub_->setAutoAnimate(false);
        sub_->setFontSize(theme::kFontCaption);
        sub_->setMarginTop(2.0f);
        sub_->setTextColor(theme::textTertiary());
        addView(sub_);

        // Una sola accion de clic: reasignar la casilla solo cambia el callback.
        registerClickAction([this](brls::View*) {
            if (onActivate_)
                onActivate_(entryIndex_);
            return true;
        });
        addGestureRecognizer(new brls::TapGestureRecognizer(this));
    }

    void setCard(const GridCardInfo& info, GameMetadataService* service,
                 Activate onActivate, Focused onFocus) {
        setVisibility(brls::Visibility::VISIBLE);
        setFocusable(true);
        entryIndex_ = info.entryIndex;
        onActivate_ = std::move(onActivate);
        onFocus_ = std::move(onFocus);

        setTextIfChanged(name_, info.title);
        setTextIfChanged(placeholder_, info.title.empty()
                                           ? std::string("?")
                                           : std::string(1, static_cast<char>(std::toupper(
                                                 static_cast<unsigned char>(info.title[0])))));
        setTextIfChanged(sub_, info.sub);
        sub_->setTextColor(info.subIsBadge ? theme::accent() : theme::textTertiary());
        modBox_->setVisibility(info.hasMods ? brls::Visibility::VISIBLE
                                            : brls::Visibility::GONE);
        favoriteBox_->setVisibility(info.favorite ? brls::Visibility::VISIBLE
                                                  : brls::Visibility::GONE);
        if (info.iconUrl.empty()) {
            image_->setVisibility(brls::Visibility::GONE);
        } else {
            image_->setVisibility(brls::Visibility::VISIBLE);
            setArtworkUrl(image_, service, info.iconUrl, currentIconUrl_, imageState_);
        }
    }

    // Celda vacia al final de la ultima fila: ocupa su hueco, no se dibuja ni
    // recibe foco.
    void setEmpty() {
        setVisibility(brls::Visibility::INVISIBLE);
        setFocusable(false);
        onActivate_ = nullptr;
        onFocus_ = nullptr;
    }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        if (onFocus_)
            onFocus_();
    }

    int entryIndex() const { return entryIndex_; }

private:
    brls::Box* cover_ = nullptr;
    AsyncRgbaImage* image_ = nullptr;
    brls::Label* placeholder_ = nullptr;
    brls::Box* modBox_ = nullptr;
    brls::Label* mod_ = nullptr;
    brls::Box* favoriteBox_ = nullptr;
    brls::Label* favorite_ = nullptr;
    brls::Label* name_ = nullptr;
    brls::Label* sub_ = nullptr;
    std::string currentIconUrl_;
    std::shared_ptr<ImageRequestState> imageState_ =
        std::make_shared<ImageRequestState>();
    int entryIndex_ = -1;
    Activate onActivate_;
    Focused onFocus_;
};

// Una fila de la cuadricula de plataforma: hasta cinco tarjetas de tamaño fijo.
// Izquierda y derecha se mueven entre ellas; arriba y abajo conservan la columna.
class RomRowCell : public brls::RecyclerCell {
public:
    static constexpr int kColumns = 5;
    static constexpr float kRowGap = 16.0f;
    static constexpr float kTextHeight = 52.0f;
    static float rowHeight() {
        return RomTile::kCoverHeight + kTextHeight + kRowGap;
    }

    explicit RomRowCell(std::shared_ptr<int> focusColumn)
        : focusColumn_(std::move(focusColumn)) {
        setFocusable(false);
        setAxis(brls::Axis::ROW);
        // Ocupa el ancho de la lista: si no, la fila mide lo que miden sus
        // hijos y las columnas no llegan al borde.
        setAlignSelf(brls::AlignSelf::STRETCH);
        setHeight(rowHeight());
        setLineBottom(0);
        setLineColor(brls::TRANSPARENT);
        for (int i = 0; i < kColumns; ++i) {
            tiles_[i] = new RomTile();
            addView(tiles_[i]);
        }
    }

    void setRow(const std::vector<GridCardInfo>& infos, GameMetadataService* service,
                RomTile::Activate onActivate) {
        activeTiles_ = static_cast<int>(std::min<size_t>(infos.size(), kColumns));
        for (int i = 0; i < kColumns; ++i) {
            if (i < activeTiles_) {
                std::shared_ptr<int> column = focusColumn_;
                tiles_[i]->setCard(infos[static_cast<size_t>(i)], service, onActivate,
                                   [column, i] { *column = i; });
            } else {
                tiles_[i]->setEmpty();
            }
        }
    }

    brls::View* getDefaultFocus() override {
        if (getVisibility() != brls::Visibility::VISIBLE || activeTiles_ <= 0)
            return brls::Box::getDefaultFocus();
        const int column = std::clamp(*focusColumn_, 0, activeTiles_ - 1);
        return tiles_[column];
    }

private:
    std::shared_ptr<int> focusColumn_;
    RomTile* tiles_[kColumns] = {};
    int activeTiles_ = 0;
};

} // namespace gamehubnx::ui
