#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "app/catalog_service.hpp"
#include "app/download_manager.hpp"
#include "app/game_metadata_service.hpp"
#include "app/favorites_service.hpp"
#include "app/gamehub_catalog.hpp"
#include "app/rom_paths.hpp"
#include "ui/catalog/catalog_helpers.hpp"
#include "ui/common/async_image.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/detail/detail_page.hpp"
#include "ui/detail/shop_addons.hpp"
#include "ui/detail/screenshot_viewer.hpp"
#include "ui/i18n.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui {

// Ficha de una ROM: lo que hace falta para decidir ANTES de descargar.
//
// Hasta ahora pulsar un titulo lo encolaba directamente, asi que no habia forma
// de ver de que juego se trataba antes de bajar tres gigas, ni de elegir entre
// sus ediciones regionales cuando las hay.
//
// La informacion la trae /api/shop/game/<id>, que se pide AL ABRIR la ficha y
// no con el indice: una descripcion son cientos de caracteres y el indice de
// gba trae mil filas.
class RomDetailActivity : public brls::Activity {
public:
    RomDetailActivity(CatalogEntry entry, gamehub::ShopPlatform platform,
                      CatalogService* catalog, GameMetadataService* metadata,
                      std::string destination,
                      std::function<void(const CatalogEntry&,
                                         const std::string&)> onDownload,
                      FavoritesService* favorites)
        : entry_(std::move(entry)), platform_(std::move(platform)),
          catalog_(catalog), metadata_(metadata),
          destination_(std::move(destination)),
          onDownload_(std::move(onDownload)), favorites_(favorites) {
        // La maqueta sale de detail_page, la misma que usa la ficha de Switch.
        // Copiarla a mano fue lo que dejo las dos pantallas descuadradas.
        auto* columns = detail_page::makeColumns();
        auto* left = detail_page::makeLeftColumn();
        cover_ = detail_page::makeCover(left);

        // Los BOTONES, como en la ficha de Switch. Antes descargar era solo la
        // accion A del marco y no habia nada que mirar ni que pulsar: la
        // pantalla parecia una lectura, no una ficha desde la que se hace algo.
        primary_ = new brls::Button();
        primary_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
        primary_->setText(tr("gamehubnx/platforms/download"));
        primary_->setMarginTop(20);
        primary_->registerClickAction([this](brls::View*) {
            startDownload();
            return true;
        });
        left->addView(primary_);

        // Solo aparece cuando hay mas de una edicion. Un boton "Versiones (1)"
        // no ofrece nada y estorba.
        versions_ = new brls::Button();
        versions_->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
        versions_->setMarginTop(8);
        versions_->setVisibility(brls::Visibility::GONE);
        versions_->registerClickAction([this](brls::View*) {
            chooseVersion();
            return true;
        });
        left->addView(versions_);

        // El ★, como en la ficha de Switch y con su mismo atajo. Cuadrado y al
        // lado, no una fila entera: no es una accion que se busque, es una que
        // se marca de paso.
        favoriteButton_ = new brls::Button();
        favoriteButton_->setStyle(&brls::BUTTONSTYLE_DEFAULT);
        favoriteButton_->setMarginTop(8);
        favoriteButton_->setWidth(64);
        favoriteButton_->setText("★");
        favoriteButton_->registerClickAction([this](brls::View*) {
            toggleFavorite();
            return true;
        });
        if (!favorites_)
            favoriteButton_->setVisibility(brls::Visibility::GONE);
        left->addView(favoriteButton_);

        // El aviso de compatibilidad va aqui, pegado al boton: es lo que decide
        // si merece la pena pulsarlo.
        warning_ = new brls::Label();
        warning_->setFontSize(theme::kFontCaption);
        warning_->setTextColor(theme::warning());
        warning_->setMarginTop(12);
        left->addView(warning_);

        columns->addView(left);

        brls::Box* right = detail_page::makeRightColumn(columns);
        right_ = right;

        title_ = new brls::Label();
        title_->setFontSize(theme::kFontHeading);
        title_->setText(entry_.title);
        right->addView(title_);


        status_ = new brls::Label();
        status_->setFontSize(theme::kFontCaption);
        status_->setTextColor(theme::textTertiary());
        status_->setMarginTop(6);
        status_->setText(tr("gamehubnx/common/loading"));
        right->addView(status_);

        // Tabla de datos, en la columna derecha y con el mismo formato que la
        // ficha de Switch: etiqueta a la izquierda, valor a la derecha. Antes
        // eran ocho lineas sueltas debajo de la portada, sin decir que era cada
        // una, y se salian por abajo de la columna.
        facts_ = new brls::Box(brls::Axis::COLUMN);
        facts_->setMarginTop(8);
        right->addView(facts_);

        description_ = new brls::Label();
        description_->setFontSize(theme::kFontSmall);
        description_->setTextColor(theme::textSecondary());
        description_->setMarginTop(16);
        right->addView(description_);

        trailer_ = new brls::Label();
        trailer_->setFontSize(theme::kFontCaption);
        trailer_->setTextColor(theme::textTertiary());
        trailer_->setMarginTop(14);
        trailer_->setVisibility(brls::Visibility::GONE);
        right->addView(trailer_);

        frame_ = new brls::AppletFrame(columns);
        frame_->setTitle(platform_.name);

        // Con lo que ya se sabe del indice la ficha YA sirve. Lo de la red solo
        // la enriquece: si el servidor es viejo y no publica /game/<id>, esto
        // se queda como esta en vez de dejar una pantalla en blanco.
        applyKnown();
        refreshFavorite();
        load();
    }

    ~RomDetailActivity() override { alive_->store(false); }

    brls::View* createContentView() override { return frame_; }

    void onContentAvailable() override {
        if (favorites_) {
            registerAction(tr("gamehubnx/catalog/action_favorite"),
                           brls::BUTTON_RT, [this](brls::View*) {
                toggleFavorite();
                return true;
            });
        }
        registerAction(tr("gamehubnx/common/close"), brls::BUTTON_B,
                       [](brls::View*) {
                           brls::Application::popActivity();
                           return true;
                       });

    }

private:
    void toggleFavorite() {
        if (!favorites_)
            return;
        std::string error;
        favorites_->toggle(entry_.infoHash, entry_.title, error);
        if (!error.empty()) {
            brls::Application::notify(error);
            return;
        }
        refreshFavorite();
    }

    void refreshFavorite() {
        if (!favoriteButton_ || !favorites_)
            return;
        // El estado se dice con el estilo del boton, no cambiando el simbolo:
        // una estrella hueca y una llena se distinguen mal a distancia de sofa.
        favoriteButton_->setStyle(favorites_->contains(entry_.infoHash)
                                      ? &brls::BUTTONSTYLE_PRIMARY
                                      : &brls::BUTTONSTYLE_DEFAULT);
    }

    void applyKnown() {
        facts_->clearViews();
        detail_page::addFactRow(facts_, tr("gamehubnx/detail/fact_platform"), platform_.name);
        if (entry_.size)
            detail_page::addFactRow(facts_, tr("gamehubnx/detail/fact_size"),
                       formatBytes(entry_.size));
        detail_page::addFactRow(facts_, tr("gamehubnx/detail/fact_release"), detail_.year);
        detail_page::addFactRow(facts_, tr("gamehubnx/detail/fact_genre"), detail_.genre);
        detail_page::addFactRow(facts_, tr("gamehubnx/detail/fact_developer"), detail_.developer);
        if (detail_.publisher != detail_.developer)
            detail_page::addFactRow(facts_, tr("gamehubnx/detail/fact_publisher"),
                       detail_.publisher);
        detail_page::addFactRow(facts_, tr("gamehubnx/detail/fact_region"), detail_.region);
        detail_page::addFactRow(facts_, tr("gamehubnx/detail/fact_languages"), detail_.languages);

        const std::string file =
            gamehub::fileNameFromUrl(entry_.directUrl);
        const roms::RomCompat compat =
            roms::displayCompat(entry_.platformSlug, file);
        if (compat == roms::RomCompat::Unplayable) {
            warning_->setText(
                !roms::ticoSupportsPlatform(entry_.platformSlug)
                    ? tr("gamehubnx/platforms/platform_warning")
                    : (platform_.emulator.empty()
                           ? tr("gamehubnx/platforms/format_warning")
                           : tr("gamehubnx/platforms/format_warning_named",
                                platform_.emulator)));
            warning_->setVisibility(brls::Visibility::VISIBLE);
        } else {
            warning_->setVisibility(brls::Visibility::GONE);
        }

        const std::string cover = detail_.coverUrl.empty() ? entry_.posterUrl
                                                           : detail_.coverUrl;
        if (!cover.empty() && metadata_)
            loadImageInto(cover_, metadata_, cover,
                          GameMetadataService::kImageDimCard);
    }

    void applyDetail() {
        if (!detail_.title.empty())
            title_->setText(detail_.title);
        applyKnown();

        description_->setText(detail_.description);
        trailer_->setText(detail_.trailerUrl.empty()
                              ? std::string()
                              : tr("gamehubnx/platforms/trailer",
                                   detail_.trailerUrl));
        trailer_->setVisibility(detail_.trailerUrl.empty()
                                    ? brls::Visibility::GONE
                                    : brls::Visibility::VISIBLE);

        // El carril compartido: mismas medidas, mismo trato de las bandas y
        // misma ruta de foco que la ficha de Switch. Se construye una sola vez,
        // al llegar la ficha; esta pantalla no se recicla como una lista.
        detail_page::addScreenshotRail(
            right_, metadata_, detail_.screenshots,
            detail_.title.empty() ? entry_.title : detail_.title, primary_);

        // El boton de versiones solo aparece cuando hay mas de una. "1 version"
        // no ofrece nada y estorba.
        if (detail_.files.size() > 1) {
            versions_->setText(
                tr("gamehubnx/platforms/versions", detail_.files.size()));
            versions_->setVisibility(brls::Visibility::VISIBLE);
        }
        status_->setText(std::string());
    }

    // Con una sola descarga se encola y ya. Con varias se pregunta: son las
    // ediciones regionales del mismo juego, y bajar la primera a ciegas es lo
    // que hacia la lista antes de existir esta pantalla.
    // Descargar la edicion principal. Elegir otra es el otro boton: mezclar
    // las dos cosas en uno obligaba a pasar por un menu incluso cuando solo hay
    // una version.
    void startDownload() {
        onDownload_(entry_, destination_);
        brls::Application::popActivity();
    }

    // Elegir version: se reutiliza la pantalla de complementos, la misma que
    // marca DLC y actualizaciones de un juego de Switch. Antes era un
    // desplegable propio que ademas solo dejaba elegir UNA, y al elegir cerraba
    // la ficha desde dentro de su propio callback -- que es lo que colgaba la
    // aplicacion al volver.
    void chooseVersion() {
        if (detail_.files.size() <= 1)
            return;
        std::vector<ShopItem> items;
        for (const gamehub::ShopGameFile& file : detail_.files) {
            // Cada version es una entrada clonada con SU url y SU tamano:
            // aguas abajo todo --el hash sintetico, el nombre del fichero, la
            // carpeta de destino-- sale de ahi.
            CatalogEntry copia = entry_;
            copia.directUrl = file.url;
            if (file.size)
                copia.size = file.size;
            if (!file.region.empty())
                copia.title = entry_.title + "  (" + file.region + ")";
            else if (!file.title.empty())
                copia.title = file.title;
            ShopItem item;
            item.entry = std::move(copia);
            item.kind = ShopItemKind::Game;
            items.push_back(std::move(item));
        }
        const std::string destination = destination_;
        auto onDownload = onDownload_;
        brls::Application::pushActivity(new ShopAddOnsActivity(
            entry_.title, std::move(items), nullptr, nullptr, nullptr,
            [onDownload, destination](std::vector<CatalogEntry> elegidas) {
                for (const CatalogEntry& una : elegidas)
                    onDownload(una, destination);
            }));
    }

    void load() {
        if (entry_.gameId <= 0) {
            // Sin id no hay ficha que pedir. Es lo que pasa con un servidor que
            // todavia no publica gh_id, y no es un error: lo que ya se sabe del
            // indice se queda en pantalla.
            status_->setText(std::string());
            return;
        }
        auto alive = alive_;
        CatalogService* catalog = catalog_;
        const int id = static_cast<int>(entry_.gameId);
        brls::async([this, alive, catalog, id] {
            gamehub::ShopGameDetail fetched;
            std::string error;
            const bool ok = catalog->fetchGameDetail(id, fetched, error);
            brls::sync([this, alive, ok, fetched = std::move(fetched)]() mutable {
                if (!alive->load())
                    return;
                if (!ok) {
                    // Un servidor viejo devuelve 404 aqui. La ficha ya tiene lo
                    // del indice, asi que se calla en vez de ensenar un error
                    // por algo que no impide nada.
                    status_->setText(std::string());
                    return;
                }
                detail_ = std::move(fetched);
                applyDetail();
            });
        });
    }

    CatalogEntry entry_;
    gamehub::ShopPlatform platform_;
    CatalogService* catalog_;
    GameMetadataService* metadata_;
    std::string destination_;
    std::function<void(const CatalogEntry&, const std::string&)> onDownload_;
    gamehub::ShopGameDetail detail_;
    FavoritesService* favorites_ = nullptr;
    brls::Button* favoriteButton_ = nullptr;

    // Mismas medidas que la ficha de Switch, para que las dos se vean iguales.
    static constexpr float kLeftColumnWidth = 320.0f;
    static constexpr float kCoverHeight = 200.0f;

    brls::AppletFrame* frame_ = nullptr;
    AsyncRgbaImage* cover_ = nullptr;
    brls::Label* title_ = nullptr;
    brls::Box* facts_ = nullptr;
    brls::Box* right_ = nullptr;
    brls::Button* primary_ = nullptr;
    brls::Button* versions_ = nullptr;
    brls::Label* warning_ = nullptr;
    brls::Label* status_ = nullptr;
    brls::Label* description_ = nullptr;
    brls::Label* trailer_ = nullptr;
    std::shared_ptr<std::atomic<bool>> alive_ =
        std::make_shared<std::atomic<bool>>(true);
};

} // namespace gamehubnx::ui
