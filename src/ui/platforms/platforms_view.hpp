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
#include "ui/platforms/rom_list.hpp"
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

class PlatformsView;

class PlatformsDataSource : public brls::RecyclerDataSource {
public:
    PlatformsDataSource(PlatformsView* owner, GameMetadataService* metadata)
        : owner_(owner), metadata_(metadata) {}

    void setPlatforms(std::vector<gamehub::ShopPlatform> items) {
        items_ = std::move(items);
    }
    const std::vector<gamehub::ShopPlatform>& items() const { return items_; }

    int numberOfRows(brls::RecyclerFrame*, int) override {
        return static_cast<int>(items_.size());
    }
    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler,
                                   brls::IndexPath index) override {
        auto* cell = static_cast<PlatformCell*>(
            recycler->dequeueReusableCell("Platform"));
        const size_t i = static_cast<size_t>(index.row);
        if (i < items_.size())
            cell->setPlatform(items_[i], metadata_);
        return cell;
    }
    float heightForRow(brls::RecyclerFrame*, brls::IndexPath) override {
        return 76.0f;
    }
    void didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) override;

private:
    PlatformsView* owner_;
    GameMetadataService* metadata_;
    std::vector<gamehub::ShopPlatform> items_;
};

// Pestana de plataformas.
//
// Switch se instala en la consola; el resto solo se descarga a una carpeta de
// la SD para un emulador. Esa diferencia la dice el servidor (campo `native`) y
// no se deduce del slug, porque de ella dependen la accion primaria, los
// complementos y la insignia de estado de todo lo que cuelga de aqui.
class PlatformsView : public brls::Box {
public:
    PlatformsView(CatalogService* catalog, GameMetadataService* metadata,
                  DownloadManager* manager)
        : brls::Box(brls::Axis::COLUMN), catalog_(catalog),
          metadata_(metadata), manager_(manager) {
        setGrow(1);
        setPadding(20, 32, 0, 32);

        status_ = new brls::Label();
        status_->setFontSize(theme::kFontSmall);
        status_->setTextColor(theme::textTertiary());
        status_->setMarginBottom(12);
        status_->setText(tr("gamehubnx/common/loading"));
        addView(status_);

        recycler_ = new brls::RecyclerFrame();
        recycler_->setGrow(1);
        recycler_->estimatedRowHeight = 76;
        recycler_->registerCell("Platform", [] { return new PlatformCell(); });
        dataSource_ = new PlatformsDataSource(this, metadata_);
        recycler_->setDataSource(dataSource_);
        recyclerHost_ = recyclerHost(recycler_);
        recyclerHost_->setVisibility(brls::Visibility::GONE);
        addView(recyclerHost_);

        load();
    }

    ~PlatformsView() override { alive_->store(false); }

    // Abre la plataforma elegida. Una sin juegos no llega hasta aqui: su fila
    // no es enfocable.
    void open(size_t index) {
        const auto& items = dataSource_->items();
        if (index >= items.size())
            return;
        // Switch ya tiene su propia pestana, con instalacion, complementos y
        // eleccion de memoria. Mandarla a la lista de ROMs seria ofrecer
        // descargar un .nsp a una carpeta donde no sirve de nada.
        if (items[index].native) {
            brls::Application::notify(tr("gamehubnx/platforms/native_hint"));
            return;
        }
        brls::Application::pushActivity(
            new RomListActivity(items[index], catalog_, manager_));
    }

private:
    void load() {
        auto alive = alive_;
        CatalogService* catalog = catalog_;
        brls::async([this, alive, catalog] {
            std::vector<gamehub::ShopPlatform> fetched;
            std::string error;
            const bool ok = catalog->fetchPlatforms(fetched, error);
            brls::sync([this, alive, ok, error = std::move(error),
                        fetched = std::move(fetched)]() mutable {
                if (!alive->load())
                    return;
                if (!ok) {
                    status_->setText(error.empty()
                        ? tr("gamehubnx/platforms/unavailable") : error);
                    return;
                }
                // Un servidor anterior a estos endpoints contesta 404 y llega
                // aqui con la lista vacia. Eso no es un fallo que ensenar como
                // error: es una tienda que todavia no publica plataformas.
                if (fetched.empty()) {
                    status_->setText(tr("gamehubnx/platforms/unavailable"));
                    return;
                }
                status_->setText(tr("gamehubnx/platforms/count",
                                    fetched.size()));
                dataSource_->setPlatforms(std::move(fetched));
                recyclerHost_->setVisibility(brls::Visibility::VISIBLE);
                recycler_->reloadData();
                brls::Application::giveFocus(recycler_);
            });
        });
    }

    CatalogService* catalog_;
    GameMetadataService* metadata_;
    DownloadManager* manager_;
    brls::Label* status_ = nullptr;
    brls::RecyclerFrame* recycler_ = nullptr;
    brls::Box* recyclerHost_ = nullptr;
    PlatformsDataSource* dataSource_ = nullptr;
    std::shared_ptr<std::atomic<bool>> alive_ =
        std::make_shared<std::atomic<bool>>(true);
};

inline void PlatformsDataSource::didSelectRowAt(brls::RecyclerFrame*,
                                                brls::IndexPath index) {
    owner_->open(static_cast<size_t>(index.row));
}

} // namespace gamehubnx::ui
