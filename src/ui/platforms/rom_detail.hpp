#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "app/catalog_service.hpp"
#include "app/download_manager.hpp"
#include "app/game_metadata_service.hpp"
#include "app/gamehub_catalog.hpp"
#include "app/rom_paths.hpp"
#include "ui/catalog/catalog_helpers.hpp"
#include "ui/common/async_image.hpp"
#include "ui/common/ui_helpers.hpp"
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
                                         const std::string&)> onDownload)
        : entry_(std::move(entry)), platform_(std::move(platform)),
          catalog_(catalog), metadata_(metadata),
          destination_(std::move(destination)),
          onDownload_(std::move(onDownload)) {
        auto* columns = new brls::Box(brls::Axis::ROW);
        columns->setGrow(1);
        columns->setPadding(18, 38, 18, 34);

        // ── Columna izquierda: portada y datos ──────────────────────────────
        //
        // Mismas medidas y mismo trato que la ficha de Switch: 320 de columna,
        // 200 de alto de portada, y el ancho salido de la PROPORCION que
        // publica la tienda. Antes era una caja de 320x320 con la imagen
        // ajustada dentro, y como las portadas de consola son verticales,
        // quedaban dos bandas negras enormes arriba y abajo -- que es lo que se
        // veia en pantalla y parecia una captura rota.
        auto* left = new brls::Box(brls::Axis::COLUMN);
        left->setWidth(kLeftColumnWidth);
        left->setMarginRight(28);

        const float coverWidth =
            std::min(kLeftColumnWidth, kCoverHeight * coverAspect());
        cover_ = new AsyncRgbaImage();
        cover_->setWidth(coverWidth);
        cover_->setHeight(kCoverHeight);
        cover_->setMarginLeft((kLeftColumnWidth - coverWidth) / 2.0f);
        cover_->setCornerRadius(theme::kRadiusLarge);
        cover_->setScalingType(brls::ImageScalingType::FIT);
        // FIT deja bandas; con el recorte puesto, borealis rellena la vista
        // entera con ellas. Es el mismo ajuste que la ficha de Switch.
        cover_->setClipsToBounds(false);
        left->addView(cover_);

        facts_ = new brls::Label();
        facts_->setFontSize(theme::kFontCaption);
        facts_->setTextColor(theme::textSecondary());
        facts_->setMarginTop(16);
        left->addView(facts_);

        // El aviso de compatibilidad va aqui y no escondido en la lista: es lo
        // que decide si merece la pena descargar esto.
        warning_ = new brls::Label();
        warning_->setFontSize(theme::kFontCaption);
        warning_->setTextColor(theme::warning());
        warning_->setMarginTop(10);
        left->addView(warning_);

        columns->addView(left);

        // ── Columna derecha: capturas y descripcion, con scroll ─────────────
        auto* right = new brls::Box(brls::Axis::COLUMN);
        right->setGrow(1);

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

        // El carril de capturas, con el alto reservado como en la ficha de
        // Switch: la fila mide mas que las imagenes para que el recuadro de
        // foco quepa sin comerse el texto de al lado, que es justo lo que
        // pasaba.
        shotsRow_ = new brls::Box(brls::Axis::ROW);
        shotsRow_->setHeight(180);
        shotsRow_->setMarginTop(14);
        shotsRow_->setVisibility(brls::Visibility::GONE);
        right->addView(shotsRow_);

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

        auto* scroll = new brls::ScrollingFrame();
        scroll->setGrow(1);
        scroll->setContentView(right);
        columns->addView(scroll);

        frame_ = new brls::AppletFrame(columns);
        frame_->setTitle(platform_.name);

        // Con lo que ya se sabe del indice la ficha YA sirve. Lo de la red solo
        // la enriquece: si el servidor es viejo y no publica /game/<id>, esto
        // se queda como esta en vez de dejar una pantalla en blanco.
        applyKnown();
        load();
    }

    ~RomDetailActivity() override { alive_->store(false); }

    brls::View* createContentView() override { return frame_; }

    void onContentAvailable() override {
        registerAction(tr("gamehubnx/common/close"), brls::BUTTON_B,
                       [](brls::View*) {
                           brls::Application::popActivity();
                           return true;
                       });
        registerAction(tr("gamehubnx/platforms/download"), brls::BUTTON_A,
                       [this](brls::View*) {
                           startDownload();
                           return true;
                       });
    }

private:
    void applyKnown() {
        std::string facts;
        auto line = [&facts](const std::string& text) {
            if (text.empty())
                return;
            if (!facts.empty())
                facts += "\n";
            facts += text;
        };
        line(platform_.name);
        if (entry_.size)
            line(formatBytes(entry_.size));
        if (!detail_.year.empty())
            line(detail_.year);
        if (!detail_.genre.empty())
            line(detail_.genre);
        if (!detail_.developer.empty())
            line(detail_.developer);
        if (!detail_.publisher.empty() &&
            detail_.publisher != detail_.developer)
            line(detail_.publisher);
        if (!detail_.region.empty())
            line(detail_.region);
        if (!detail_.languages.empty())
            line(detail_.languages);
        facts_->setText(facts);

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

        // Las miniaturas se crean una sola vez, al llegar la ficha: esta
        // pantalla no se recicla como una lista.
        if (!detail_.screenshots.empty() && metadata_) {
            const std::vector<std::string> shots = detail_.screenshots;
            const std::string viewerTitle =
                detail_.title.empty() ? entry_.title : detail_.title;
            for (size_t i = 0; i < shots.size(); ++i) {
                auto* thumb = new AsyncRgbaImage();
                thumb->setWidth(300);
                thumb->setHeight(170);
                thumb->setMarginRight(12);
                thumb->setCornerRadius(theme::kRadiusSmall);
                thumb->setScalingType(brls::ImageScalingType::FIT);
                // Sin recorte, para que FIT no pinte las bandas dentro de la
                // vista. Mismo ajuste que la portada y que la ficha de Switch.
                thumb->setClipsToBounds(false);
                thumb->setFocusable(true);
                // La ficha de Switch enruta ARRIBA a su boton principal porque
                // el carril seria un callejon sin salida. Aqui NO se hace: no
                // hay ningun otro enfocable en la pantalla --descargar es una
                // accion del marco, con A-- asi que no habria a donde ir.
                // Apuntar a una etiqueta seria aparentar un arreglo sin serlo.
                thumb->registerClickAction(
                    [this, shots, i, viewerTitle](brls::View*) {
                        brls::Application::pushActivity(
                            new ScreenshotViewerActivity(metadata_, shots, i,
                                                         viewerTitle));
                        return true;
                    });
                shotsRow_->addView(thumb);
                loadImageInto(thumb, metadata_, shots[i],
                              GameMetadataService::kImageDimCard);
            }
            shotsRow_->setVisibility(brls::Visibility::VISIBLE);
        }

        // El recuento de versiones solo se dice cuando hay mas de una: "1
        // versión" no es informacion, es ruido.
        status_->setText(detail_.files.size() > 1
                             ? tr("gamehubnx/platforms/versions",
                                  detail_.files.size())
                             : std::string());
    }

    // Con una sola descarga se encola y ya. Con varias se pregunta: son las
    // ediciones regionales del mismo juego, y bajar la primera a ciegas es lo
    // que hacia la lista antes de existir esta pantalla.
    void startDownload() {
        if (detail_.files.size() <= 1) {
            onDownload_(entry_, destination_);
            brls::Application::popActivity();
            return;
        }
        std::vector<std::string> labels;
        for (const gamehub::ShopGameFile& file : detail_.files) {
            std::string label = file.region.empty()
                ? (file.title.empty() ? file.kind : file.title)
                : file.region;
            if (file.size)
                label += "   " + formatBytes(file.size);
            labels.push_back(label);
        }
        const std::vector<gamehub::ShopGameFile> files = detail_.files;
        auto* dropdown = new brls::Dropdown(
            tr("gamehubnx/platforms/which_version"), labels,
            [this, files](int chosen) {
                if (chosen < 0 || chosen >= static_cast<int>(files.size()))
                    return;
                // La entrada se clona con la URL elegida: aguas abajo todo
                // --el hash sintetico, el nombre del fichero, la carpeta-- sale
                // de ahi, asi que cambiarla es cambiar la descarga entera.
                CatalogEntry picked = entry_;
                picked.directUrl = files[static_cast<size_t>(chosen)].url;
                if (files[static_cast<size_t>(chosen)].size)
                    picked.size = files[static_cast<size_t>(chosen)].size;
                onDownload_(picked, destination_);
                brls::Application::popActivity();
            },
            0);
        brls::Application::pushActivity(new brls::Activity(dropdown));
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

    // Mismas medidas que la ficha de Switch, para que las dos se vean iguales.
    static constexpr float kLeftColumnWidth = 320.0f;
    static constexpr float kCoverHeight = 200.0f;

    brls::AppletFrame* frame_ = nullptr;
    AsyncRgbaImage* cover_ = nullptr;
    brls::Label* title_ = nullptr;
    brls::Label* facts_ = nullptr;
    brls::Label* warning_ = nullptr;
    brls::Label* status_ = nullptr;
    brls::Box* shotsRow_ = nullptr;
    brls::Label* description_ = nullptr;
    brls::Label* trailer_ = nullptr;
    std::shared_ptr<std::atomic<bool>> alive_ =
        std::make_shared<std::atomic<bool>>(true);
};

} // namespace gamehubnx::ui
