#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "app/catalog_service.hpp"
#include "app/app_settings.hpp"
#include "app/download_manager.hpp"
#include "app/gamehub_catalog.hpp"
#include "app/rom_paths.hpp"
#include "ui/catalog/catalog_helpers.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/i18n.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui {

class RomListActivity;

// Fila de una ROM.
//
// Lleva a la izquierda una franja con el veredicto de compatibilidad --verde
// lo abre el emulador, ambar no lo abre, gris no consta-- y, SOLO cuando no
// lo abre, una insignia con la extension pegada al titulo.
//
// La insignia no se pone tambien en verde a proposito: repetir ".GBA" en las
// mil filas de GBA no informa de nada, solo hace ruido. El color de la franja
// es lo que se lee de un vistazo y la insignia queda para el caso que hay que
// mirar dos veces. Y van color Y texto: en una tele lavada, o para quien no
// distingue el rojo del verde, el color solo no dice nada.
class RomCell : public brls::RecyclerCell {
public:
    RomCell() {
        setFocusable(true);
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setPadding(6, 20, 6, 8);
        setHeight(62);

        stripe_ = new brls::Box();
        stripe_->setFocusable(false);
        stripe_->setWidth(4);
        stripe_->setHeight(34);
        stripe_->setCornerRadius(2);
        stripe_->setMarginRight(12);
        addView(stripe_);

        auto* text = new brls::Box(brls::Axis::COLUMN);
        text->setGrow(1);

        auto* titleRow = new brls::Box(brls::Axis::ROW);
        titleRow->setAlignItems(brls::AlignItems::CENTER);
        title_ = new brls::Label();
        title_->setSingleLine(true);
        title_->setAutoAnimate(false);
        title_->setFontSize(theme::kFontBody);
        // El titulo cede sitio y la insignia no: un nombre largo recorta el
        // nombre, que se puede adivinar, antes que el aviso, que no.
        title_->setShrink(1);
        titleRow->addView(title_);

        chip_ = new brls::Box();
        chip_->setFocusable(false);
        chip_->setShrink(0);
        chip_->setMarginLeft(10);
        chip_->setPadding(1, 8, 1, 8);
        chip_->setCornerRadius(theme::kRadiusSmall);
        chip_->setBorderColor(theme::warning());
        chip_->setBorderThickness(1.0f);
        chip_->setAlignItems(brls::AlignItems::CENTER);
        chip_->setJustifyContent(brls::JustifyContent::CENTER);
        chipLabel_ = new brls::Label();
        chipLabel_->setFontSize(theme::kFontCaption);
        chipLabel_->setTextColor(theme::warning());
        chip_->addView(chipLabel_);
        titleRow->addView(chip_);
        text->addView(titleRow);

        meta_ = new brls::Label();
        meta_->setSingleLine(true);
        meta_->setAutoAnimate(false);
        meta_->setFontSize(theme::kFontCaption);
        meta_->setTextColor(theme::textTertiary());
        meta_->setMarginTop(3);
        text->addView(meta_);
        addView(text);

        badge_ = new brls::Label();
        badge_->setFontSize(theme::kFontCaption);
        badge_->setMarginLeft(16);
        badge_->setTextColor(theme::success());
        addView(badge_);
    }

    void setRom(const CatalogEntry& entry, bool onSdCard,
                roms::RomCompat compat, const std::string& ext,
                const std::string& emulator) {
        setTextIfChanged(title_, entry.title);

        // Verde / ambar / gris. El gris NO es un "va mal": es que de esa
        // plataforma no consta que abre el nucleo, y entonces la app no
        // afirma nada.
        NVGcolor mark = theme::textDisabled();
        if (compat == roms::RomCompat::Playable)
            mark = theme::success();
        else if (compat == roms::RomCompat::Unplayable)
            mark = theme::warning();
        stripe_->setBackgroundColor(mark);

        const bool unplayable = compat == roms::RomCompat::Unplayable;
        chip_->setVisibility(unplayable ? brls::Visibility::VISIBLE
                                        : brls::Visibility::GONE);
        if (unplayable)
            setTextIfChanged(chipLabel_, upperAscii(ext));

        std::string meta = entry.size ? formatBytes(entry.size) : std::string();
        // El servidor cataloga por lo que escanea; el emulador abre otra cosa.
        // Descargar un .cia de 3DS funciona y luego no aparece en el emulador,
        // sin ningun error que lo explique. Se dice aqui, antes de bajarlo.
        if (unplayable) {
            if (!meta.empty())
                meta += "   ";
            // Con el nombre del emulador el aviso deja de ser una queja vaga y
            // se puede comprobar; sin el, la frase generica.
            meta += emulator.empty()
                        ? tr("gamehubnx/platforms/format_warning")
                        : tr("gamehubnx/platforms/format_warning_named",
                             emulator);
        }
        setTextIfChanged(meta_, meta);
        meta_->setTextColor(unplayable ? theme::warning()
                                       : theme::textTertiary());
        // "En la SD", no "instalado": una ROM no se instala en ningun sitio, y
        // lo que se afirma aqui es exactamente lo que se ha comprobado --que el
        // fichero esta en la carpeta de destino--.
        setTextIfChanged(badge_, onSdCard ? tr("gamehubnx/platforms/on_sd")
                                          : std::string());
    }

private:
    static std::string upperAscii(std::string s) {
        for (char& c : s)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }

    brls::Box* stripe_;
    brls::Label* title_;
    brls::Box* chip_;
    brls::Label* chipLabel_;
    brls::Label* meta_;
    brls::Label* badge_;
};

class RomListActivity;

class RomDataSource : public brls::RecyclerDataSource {
public:
    explicit RomDataSource(RomListActivity* owner) : owner_(owner) {}

    void setRoms(std::vector<CatalogEntry> roms) { roms_ = std::move(roms); }
    // Lo publica el servidor por plataforma; puede venir vacio.
    void setEmulator(std::string name) { emulator_ = std::move(name); }
    const std::vector<CatalogEntry>& roms() const { return roms_; }

    int numberOfRows(brls::RecyclerFrame*, int) override {
        return static_cast<int>(roms_.size());
    }
    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler,
                                   brls::IndexPath index) override;
    float heightForRow(brls::RecyclerFrame*, brls::IndexPath) override {
        return 62.0f;
    }
    void didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) override;

private:
    RomListActivity* owner_;
    std::vector<CatalogEntry> roms_;
    std::string emulator_;
};

// Catalogo de una plataforma emulada.
//
// No hay "instalar", ni complementos, ni eleccion de memoria: una ROM es un
// fichero que se copia a una carpeta y lo abre otro programa. La cabecera dice
// esa carpeta con todas sus letras, porque es lo unico que el usuario necesita
// saber para encontrarla despues.
class RomListActivity : public brls::Activity {
public:
    RomListActivity(gamehub::ShopPlatform platform, CatalogService* catalog,
                    DownloadManager* manager, AppSettings* settings)
        : platform_(std::move(platform)), catalog_(catalog),
          manager_(manager), settings_(settings) {
        destination_ = roms::defaultRomPath(platform_.slug);

        auto* content = new brls::Box(brls::Axis::COLUMN);
        content->setGrow(1);
        content->setPadding(18, 38, 18, 34);

        auto* path = new brls::Label();
        path->setFontSize(theme::kFontSmall);
        path->setTextColor(theme::textSecondary());
        path->setText(tr("gamehubnx/platforms/saved_to") + "  " + destination_);
        content->addView(path);

        // Cuando la carpeta NO sale de la tabla comprobada de tico se avisa: la
        // ROM se descargaria igual, pero el emulador no la encontraria y eso no
        // da ningun error visible. Mejor decirlo antes que dejar que pase.
        // Si el arbol de tico no esta, dejar ahi la ROM la esconde en una
        // carpeta que nadie mira. Se dice antes de descargar, no despues.
        if (!roms::ticoRomsPresent()) {
            auto* missing = new brls::Label();
            missing->setFontSize(theme::kFontCaption);
            missing->setTextColor(theme::warning());
            missing->setMarginTop(4);
            missing->setText(tr("gamehubnx/platforms/tico_missing"));
            content->addView(missing);
        }
        if (!roms::defaultPathIsKnown(platform_.slug)) {
            auto* warn = new brls::Label();
            warn->setFontSize(theme::kFontCaption);
            warn->setTextColor(theme::warning());
            warn->setMarginTop(4);
            warn->setText(tr("gamehubnx/platforms/path_unverified"));
            content->addView(warn);
        }

        status_ = new brls::Label();
        status_->setFontSize(theme::kFontCaption);
        status_->setTextColor(theme::textTertiary());
        status_->setMarginTop(8);
        status_->setMarginBottom(10);
        status_->setText(tr("gamehubnx/common/loading"));
        content->addView(status_);

        recycler_ = new brls::RecyclerFrame();
        recycler_->setGrow(1);
        recycler_->estimatedRowHeight = 62;
        recycler_->registerCell("Rom", [] { return new RomCell(); });
        dataSource_ = new RomDataSource(this);
        dataSource_->setEmulator(platform_.emulator);
        recycler_->setDataSource(dataSource_);
        recyclerHost_ = recyclerHost(recycler_);
        recyclerHost_->setVisibility(brls::Visibility::GONE);
        content->addView(recyclerHost_);

        frame_ = new brls::AppletFrame(content);
        frame_->setTitle(platform_.name);
        load();
    }

    ~RomListActivity() override { alive_->store(false); }

    brls::View* createContentView() override { return frame_; }

    void onContentAvailable() override {
        registerAction(tr("gamehubnx/common/close"), brls::BUTTON_B,
                       [](brls::View*) {
                           brls::Application::popActivity();
                           return true;
                       });
    }

    void download(size_t index) {
        const std::vector<CatalogEntry>& roms = dataSource_->roms();
        if (index >= roms.size())
            return;

        // Preguntar o no lo decide el ajuste. Por defecto no se pregunta:
        // quien tiene tico quiere que la ROM aparezca ahi y punto, y un modal
        // en cada descarga seria un estorbo.
        const bool ask = settings_ && settings_->get().askRomDestination;
        if (!ask) {
            enqueue(index, destination_);
            return;
        }

        auto* dialog = new brls::Dialog(
            tr("gamehubnx/platforms/where") + "\n" + destination_);
        dialog->addButton(tr("gamehubnx/platforms/to_emulator"),
                          [this, index] { enqueue(index, destination_); });
        // La otra opcion no es "otra carpeta cualquiera" --no hay teclado de
        // rutas aqui-- sino dejarlo en descargas, que es donde estaba antes de
        // que existiera el traslado y sigue siendo util para sacarlo por FTP.
        dialog->addButton(tr("gamehubnx/platforms/to_downloads"),
                          [this, index] { enqueue(index, std::string()); });
        dialog->open();
    }

private:
    void enqueue(size_t index, const std::string& destination) {
        const std::vector<CatalogEntry>& roms = dataSource_->roms();
        if (index >= roms.size())
            return;
        const CatalogEntry& rom = roms[index];

        DebridImport import;
        import.infoHash = catalogLower(rom.infoHash);
        import.name = rom.title;
        import.totalBytes = rom.size;
        import.debridId = rom.transferHandle();
        // Descarga y punto: no hay paquete que instalar, asi que ni modo de
        // instalacion ni destino de memoria. Es toda la diferencia con Switch.
        import.mode = TransferMode::DownloadOnly;
        import.fileSelection = {static_cast<uint8_t>(FileAction::Download)};
        import.packageCount = 0;
        // Y aqui es donde tiene que acabar. Sin esto el rotulo de arriba era
        // una promesa que el codigo no cumplia: el fichero se quedaba en
        // downloads/.
        import.destinationDir = destination;

        std::string taskId;
        std::string error;
        if (!manager_->importDebrid(import, taskId, error)) {
            brls::Application::notify(error);
            return;
        }
        brls::Application::notify(
            tr("gamehubnx/platforms/downloading", rom.title));
    }

private:
    void load() {
        auto alive = alive_;
        CatalogService* catalog = catalog_;
        const std::string slug = platform_.slug;
        brls::async([this, alive, catalog, slug] {
            std::vector<CatalogEntry> fetched;
            std::string error;
            const bool ok = catalog->fetchPlatformIndex(slug, fetched, error);
            brls::sync([this, alive, ok, error = std::move(error),
                        fetched = std::move(fetched)]() mutable {
                if (!alive->load())
                    return;
                if (!ok) {
                    status_->setText(error);
                    return;
                }
                // Cuantos de esos juegos no va a poder abrir el emulador.
                // El dato solo existe aqui --hace falta el indice entero-- y
                // es lo que convierte las franjas de color en algo que se
                // entiende sin leyenda.
                size_t unplayable = 0;
                for (const CatalogEntry& rom : fetched)
                    if (roms::isUnplayableByEmulator(
                            rom.platformSlug,
                            gamehub::fileNameFromUrl(rom.directUrl)))
                        ++unplayable;
                status_->setText(summaryText(fetched.size(), unplayable));
                status_->setTextColor(unplayable ? theme::warning()
                                                 : theme::textTertiary());
                dataSource_->setRoms(std::move(fetched));
                recyclerHost_->setVisibility(brls::Visibility::VISIBLE);
                recycler_->reloadData();
                brls::Application::giveFocus(recycler_);
            });
        });
    }

    // "63 juegos", o "63 juegos - 41 no los abre Azahar" cuando hay algo que
    // decir. El nombre del emulador lo publica el servidor; sin el, la frase
    // generica.
    std::string summaryText(size_t total, size_t unplayable) const {
        if (unplayable == 0)
            return tr("gamehubnx/platforms/games", total);
        if (platform_.emulator.empty())
            return tr("gamehubnx/platforms/games_unplayable_plain", total,
                      unplayable);
        return tr("gamehubnx/platforms/games_unplayable", total, unplayable,
                  platform_.emulator);
    }

    gamehub::ShopPlatform platform_;
    CatalogService* catalog_;
    DownloadManager* manager_;
    AppSettings* settings_;
    std::string destination_;
    brls::AppletFrame* frame_ = nullptr;
    brls::Label* status_ = nullptr;
    brls::RecyclerFrame* recycler_ = nullptr;
    brls::Box* recyclerHost_ = nullptr;
    RomDataSource* dataSource_ = nullptr;
    std::shared_ptr<std::atomic<bool>> alive_ =
        std::make_shared<std::atomic<bool>>(true);
};

inline brls::RecyclerCell* RomDataSource::cellForRow(
    brls::RecyclerFrame* recycler, brls::IndexPath index) {
    auto* cell = static_cast<RomCell*>(recycler->dequeueReusableCell("Rom"));
    const size_t i = static_cast<size_t>(index.row);
    if (i < roms_.size()) {
        const std::string file =
            gamehub::fileNameFromUrl(roms_[i].directUrl);
        cell->setRom(roms_[i], /*onSdCard=*/false,
                     roms::romCompat(roms_[i].platformSlug, file),
                     roms::extensionOf(file), emulator_);
    }
    return cell;
}

inline void RomDataSource::didSelectRowAt(brls::RecyclerFrame*,
                                           brls::IndexPath index) {
    owner_->download(static_cast<size_t>(index.row));
}

} // namespace gamehubnx::ui
