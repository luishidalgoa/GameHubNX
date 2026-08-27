#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "app/catalog_service.hpp"
#include "app/gamehub_catalog.hpp"
#include "app/game_metadata_service.hpp"
#include "app/rom_paths.hpp"
#include "ui/common/async_image.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/i18n.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui {

// Fila de una plataforma.
//
// El icono lo publica GameHub (gh_icon), es el mismo que se ve en su web. Se
// dibuja uno propio SOLO cuando el servidor no manda ninguno: tener dos juegos
// de iconos para lo mismo garantiza que acaben distintos.
class PlatformCell : public brls::RecyclerCell {
public:
    PlatformCell() {
        setFocusable(true);
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setPadding(8, 20, 8, 20);
        setHeight(76);

        icon_ = new AsyncRgbaImage();
        icon_->setWidth(44);
        icon_->setHeight(44);
        icon_->setMarginRight(18);
        icon_->setScalingType(brls::ImageScalingType::FIT);
        addView(icon_);

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

    void setPlatform(const gamehub::ShopPlatform& p,
                     GameMetadataService* metadata) {
        setTextIfChanged(name_, p.name);

        std::string meta = tr("gamehubnx/platforms/games", p.games);
        for (const std::string& ext : p.extensions)
            meta += "   " + ext;
        setTextIfChanged(meta_, meta);

        // "Nativa" lo dice el servidor; no se deduce del slug.
        if (p.native) {
            setTextIfChanged(badge_, tr("gamehubnx/platforms/native"));
            badge_->setTextColor(theme::accent());
        } else if (!p.emulator.empty()) {
            setTextIfChanged(badge_, p.emulator);
            badge_->setTextColor(theme::textTertiary());
        } else {
            setTextIfChanged(badge_, std::string());
        }

        // Una plataforma sin juegos se ve pero no se entra: esconderla haria
        // pensar que el servidor no la tiene.
        setFocusable(p.games > 0);
        const NVGcolor ink = p.games > 0 ? theme::textPrimary()
                                         : theme::textDisabled();
        name_->setTextColor(ink);

        if (!p.iconUrl.empty() && metadata)
            setArtworkUrl(icon_, metadata, p.iconUrl, iconUrl_, iconState_,
                          /*maxDim=*/128);
    }

private:
    AsyncRgbaImage* icon_;
    brls::Label* name_;
    brls::Label* meta_;
    brls::Label* badge_;
    std::string iconUrl_;
    std::shared_ptr<ImageRequestState> iconState_ =
        std::make_shared<ImageRequestState>();
};

} // namespace gamehubnx::ui
