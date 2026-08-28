#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "app/catalog_service.hpp"
#include "app/app_settings.hpp"
#include "app/download_manager.hpp"
#include "app/favorites_service.hpp"
#include "app/gamehub_catalog.hpp"
#include "app/rom_paths.hpp"
#include "ui/catalog/catalog_grid.hpp"
#include "ui/catalog/catalog_helpers.hpp"
#include "ui/platforms/rom_detail.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/i18n.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui {

class RomListActivity;

// Rejilla de ROMs, la misma que el catalogo de Switch.
//
// Antes era una lista de texto y desentonaba: la misma aplicacion ensenaba los
// juegos de Switch con su portada y los de las demas consolas como renglones.
// Se reutilizan GridRowCell y GameCard tal cual, que es lo que garantiza que se
// vean IGUAL y no solo parecido.
//
// El aviso de compatibilidad viaja en la linea de debajo del titulo --el `sub`
// de la tarjeta-- como insignia, que es el hueco que esa tarjeta ya tiene para
// decir algo del juego.
// ".cia" -> ".CIA". Es lo que se pinta en la insignia de la tarjeta.
inline std::string upperExtension(std::string ext) {
    for (char& c : ext)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return ext;
}

class RomDataSource : public brls::RecyclerDataSource {
public:
    RomDataSource(RomListActivity* owner, GameMetadataService* metadata,
                  std::shared_ptr<int> focusColumn,
                  FavoritesService* favorites)
        : owner_(owner), metadata_(metadata),
          focusColumn_(std::move(focusColumn)), favorites_(favorites) {}

    void setRoms(std::vector<CatalogEntry> roms) { roms_ = std::move(roms); }
    // Lo publica el servidor por plataforma; puede venir vacio.
    void setEmulator(std::string name) { emulator_ = std::move(name); }
    const std::vector<CatalogEntry>& roms() const { return roms_; }

    int numberOfRows(brls::RecyclerFrame*, int) override {
        return (static_cast<int>(roms_.size()) + grid::kColumns - 1) /
               grid::kColumns;
    }
    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler,
                                   brls::IndexPath index) override;
    float heightForRow(brls::RecyclerFrame*, brls::IndexPath) override {
        return grid::kRowHeight;
    }
    // Nada: la tarjeta se activa sola con su propio manejador, como en el
    // catalogo. Dejarlo aqui ademas abriria la ficha dos veces.
    void didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath) override {}

private:
    GridCardInfo makeInfo(size_t index) const;

    RomListActivity* owner_;
    GameMetadataService* metadata_;
    std::shared_ptr<int> focusColumn_;
    FavoritesService* favorites_;
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
                    DownloadManager* manager, AppSettings* settings,
                    GameMetadataService* metadata,
                    FavoritesService* favorites)
        : platform_(std::move(platform)), catalog_(catalog),
          manager_(manager), settings_(settings), metadata_(metadata),
          favorites_(favorites) {
        destination_ = resolvedDestination();

        auto* content = new brls::Box(brls::Axis::COLUMN);
        content->setGrow(1);
        content->setPadding(18, 38, 18, 34);

        pathLabel_ = new brls::Label();
        pathLabel_->setFontSize(theme::kFontSmall);
        pathLabel_->setTextColor(theme::textSecondary());
        content->addView(pathLabel_);

        // El aviso de "carpeta sin confirmar" deja de tener sentido en cuanto
        // el usuario elige una a mano, asi que se pinta junto con la ruta y no
        // una sola vez al construir.
        pathWarning_ = new brls::Label();
        pathWarning_->setFontSize(theme::kFontCaption);
        pathWarning_->setTextColor(theme::warning());
        pathWarning_->setMarginTop(4);
        content->addView(pathWarning_);
        refreshPathLabels();

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

        // Fila de cabecera: el recuento a la izquierda y la chapa de favoritos
        // a la derecha, como en el catalogo de Switch.
        auto* headerRow = new brls::Box(brls::Axis::ROW);
        headerRow->setAlignItems(brls::AlignItems::CENTER);
        headerRow->setMarginTop(8);
        headerRow->setMarginBottom(10);

        status_ = new brls::Label();
        status_->setFontSize(theme::kFontCaption);
        status_->setTextColor(theme::textTertiary());
        status_->setGrow(1);
        status_->setText(tr("gamehubnx/common/loading"));
        headerRow->addView(status_);

        favoritesChip_ = new brls::Button();
        favoritesChip_->setStyle(&brls::BUTTONSTYLE_DEFAULT);
        favoritesChip_->setHeight(40);
        favoritesChip_->setFontSize(theme::kFontCaption);
        favoritesChip_->setShrink(0.0f);
        favoritesChip_->setPaddingLeft(14);
        favoritesChip_->setPaddingRight(14);
        favoritesChip_->setText("★");
        favoritesChip_->registerClickAction([this](brls::View*) {
            favoritesOnly_ = !favoritesOnly_;
            rebuild();
            return true;
        });
        // Sin servicio de favoritos no se ofrece un filtro que no puede
        // responder nada.
        if (!favorites_)
            favoritesChip_->setVisibility(brls::Visibility::GONE);
        headerRow->addView(favoritesChip_);
        content->addView(headerRow);

        recycler_ = new brls::RecyclerFrame();
        recycler_->setGrow(1);
        recycler_->estimatedRowHeight = grid::kRowHeight;
        recycler_->registerCell("GridRow", [column = focusColumn_] {
            return new GridRowCell(column);
        });
        dataSource_ = new RomDataSource(this, metadata_, focusColumn_,
                                        favorites_);
        dataSource_->setEmulator(platform_.emulator);
        recycler_->setDataSource(dataSource_);
        recyclerHost_ = recyclerHost(recycler_);
        recyclerHost_->setVisibility(brls::Visibility::GONE);
        content->addView(recyclerHost_);

        frame_ = new brls::AppletFrame(content);
        frame_->setTitle(platform_.name);
        load();
    }

    ~RomListActivity() override {
        alive_->store(false);
        // La proporcion es un ajuste GLOBAL que comparten esta pantalla y el
        // catalogo de Switch. Se devuelve al salir: si no, las cajas de Switch
        // se quedarian con la forma de la ultima consola que se abrio.
        if (previousAspect_ > 0.0f)
            setCoverAspect(previousAspect_);
    }

    brls::View* createContentView() override { return frame_; }

    void onContentAvailable() override {
        registerAction(tr("gamehubnx/common/close"), brls::BUTTON_B,
                       [](brls::View*) {
                           brls::Application::popActivity();
                           return true;
                       });
        if (favorites_) {
            registerAction(tr("gamehubnx/catalog/action_favorite"),
                           brls::BUTTON_RT, [this](brls::View*) {
                toggleFocusedFavorite();
                return true;
            });
        }
        registerAction(tr("gamehubnx/platforms/change_folder"), brls::BUTTON_Y,
                       [this](brls::View*) {
                           chooseFolder();
                           return true;
                       });
    }

    // Elegir donde van las ROMs de ESTA plataforma.
    //
    // Lo primero de la lista son las carpetas que existen de verdad bajo la
    // raiz de tico: para nds o 3ds no hay nombre confirmado y la respuesta esta
    // en la tarjeta, no en una tabla que yo pueda ampliar adivinando. Y detras,
    // escribir una ruta a mano, porque no todo el mundo usa tico y su arbol no
    // tiene por que valerle a quien use otro emulador.
    void chooseFolder() {
        std::vector<std::string> labels;
        std::vector<std::string> paths;   // vacio = accion, no ruta
        for (const std::string& folder : roms::listRomFolders(roms::kTicoRoot)) {
            const std::string full = std::string(roms::kTicoRoot) + "/" + folder;
            labels.push_back(full);
            paths.push_back(full);
        }
        labels.push_back(tr("gamehubnx/platforms/type_path"));
        paths.push_back(std::string());
        const size_t typeIndex = labels.size() - 1;

        size_t defaultIndex = labels.size();
        const std::string byDefault = roms::defaultRomPath(platform_.slug);
        labels.push_back(tr("gamehubnx/platforms/use_default") + "  " +
                         byDefault);
        paths.push_back(byDefault);

        // Sin carpetas encontradas la lista se queda en las dos ultimas
        // opciones, y aun asi se abre: escribir la ruta a mano sigue estando,
        // que es justo lo que necesita quien no usa tico.

        auto* dropdown = new brls::Dropdown(
            tr("gamehubnx/platforms/choose_folder"), labels,
            [this, paths, typeIndex, defaultIndex](int index) {
                if (index < 0 || index >= static_cast<int>(paths.size()))
                    return;
                const size_t i = static_cast<size_t>(index);
                if (i == typeIndex) {
                    typeFolder();
                    return;
                }
                // "Por defecto" borra la entrada en vez de guardar la ruta de
                // hoy: si manana se corrige el nombre de carpeta, esta
                // plataforma se corrige sola.
                applyFolder(paths[i], /*clear=*/i == defaultIndex);
            },
            0);
        brls::Application::pushActivity(new brls::Activity(dropdown));
    }

    void typeFolder() {
        brls::Application::getImeManager()->openForText(
            [this](std::string text) {
                const size_t first = text.find_first_not_of(" \t\r\n");
                const size_t last = text.find_last_not_of(" \t\r\n");
                std::string path = first == std::string::npos
                    ? std::string()
                    : text.substr(first, last - first + 1);
                while (path.size() > 1 && path.back() == '/')
                    path.pop_back();
                if (path.empty())
                    return;
                // Sin prefijo no hay nada que abrir: en la consola las rutas
                // viven en sdmc:. Se completa en vez de rechazar, que teclear
                // esto con la cruceta ya cuesta bastante.
                if (path.rfind("sdmc:", 0) != 0)
                    path = "sdmc:" + (path[0] == '/' ? path : "/" + path);
                applyFolder(path, /*clear=*/false);
            },
            tr("gamehubnx/platforms/type_path_prompt"), /*subText=*/"",
            /*maxLength=*/255, /*initialText=*/destination_,
            brls::KEYBOARD_DISABLE_NONE);
    }

    void applyFolder(const std::string& path, bool clear) {
        if (!settings_)
            return;
        AppSettingsData values = settings_->get();
        if (clear)
            values.romPaths.erase(platform_.slug);
        else
            values.romPaths[platform_.slug] = path;
        std::string error;
        if (!settings_->update(values, error)) {
            brls::Application::notify(error);
            return;
        }
        destination_ = resolvedDestination();
        refreshPathLabels();
    }

    void openDetail(size_t index) {
        const std::vector<CatalogEntry>& roms = dataSource_->roms();
        if (index >= roms.size())
            return;
        brls::Application::pushActivity(new RomDetailActivity(
            roms[index], platform_, catalog_, metadata_, destination_,
            [this](const CatalogEntry& rom, const std::string& destination) {
                enqueueEntry(rom, destination);
            },
            favorites_));
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
        enqueueEntry(roms[index], destination);
    }

    // Por entrada y no por indice: la version que elige la ficha --una edicion
    // regional-- no esta en la lista, se construye clonando la entrada con otra
    // URL.
    void enqueueEntry(const CatalogEntry& rom, const std::string& destination) {
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
            float aspect = 0.0f;
            const bool ok =
                catalog->fetchPlatformIndex(slug, fetched, error, &aspect);
            brls::sync([this, alive, ok, aspect, error = std::move(error),
                        fetched = std::move(fetched)]() mutable {
                if (!alive->load())
                    return;
                // La forma de la caja de ESTA consola, antes de crear ninguna
                // tarjeta. Sin esto se pintaban todas con la de Switch y las
                // portadas salian con bandas negras arriba y abajo.
                if (aspect > 0.0f) {
                    previousAspect_ = coverAspect();
                    setCoverAspect(aspect);
                }
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
                    if (roms::displayCompat(
                            rom.platformSlug,
                            gamehub::fileNameFromUrl(rom.directUrl)) ==
                        roms::RomCompat::Unplayable)
                        ++unplayable;
                summary_ = summaryText(fetched.size(), unplayable);
                summaryWarns_ = unplayable > 0;
                // Se guarda la lista COMPLETA: el filtro de favoritos es una
                // vista, no una carga distinta, asi que quitarlo no puede
                // obligar a volver a pedir el indice.
                allRoms_ = std::move(fetched);
                rebuild();
                // Aqui SI se toma el foco: esta pantalla se abre a proposito
                // eligiendo una plataforma, no se cruza uno con ella navegando.
                brls::Application::giveFocus(recycler_);
            });
        });
    }

    std::string resolvedDestination() const {
        static const std::map<std::string, std::string> kNone;
        return roms::romPathFor(platform_.slug,
                                settings_ ? settings_->get().romPaths : kNone);
    }

    void refreshPathLabels() {
        pathLabel_->setText(tr("gamehubnx/platforms/saved_to") + "  " +
                            destination_);
        // El aviso solo sale cuando la carpeta la eligio la tabla y no consta:
        // en cuanto el usuario pone una a mano, quien responde por ella es el.
        const bool chosen = settings_ &&
            settings_->get().romPaths.count(platform_.slug) > 0;
        const bool unverified =
            !chosen && !roms::defaultPathIsKnown(platform_.slug);
        pathWarning_->setText(unverified
                                  ? tr("gamehubnx/platforms/path_unverified")
                                  : std::string());
        pathWarning_->setVisibility(unverified ? brls::Visibility::VISIBLE
                                               : brls::Visibility::GONE);
    }

    // "63 juegos", o "63 juegos - 41 no los abre Azahar" cuando hay algo que
    // decir. El nombre del emulador lo publica el servidor; sin el, la frase
    // generica.
    // Aplica el filtro y repinta. Con el filtro puesto y nada marcado se dice,
    // en vez de dejar una rejilla vacia sin explicacion.
    void rebuild() {
        std::vector<CatalogEntry> shown;
        for (const CatalogEntry& rom : allRoms_) {
            if (favoritesOnly_ && favorites_ && !favorites_->contains(rom.infoHash))
                continue;
            shown.push_back(rom);
        }
        const size_t count = shown.size();
        dataSource_->setRoms(std::move(shown));
        if (favoritesOnly_ && count == 0)
            status_->setText(tr("gamehubnx/platforms/no_favorites"));
        else
            status_->setText(summary_);
        status_->setTextColor(summaryWarns_ && !favoritesOnly_
                                  ? theme::warning()
                                  : theme::textTertiary());
        if (favoritesChip_)
            favoritesChip_->setStyle(favoritesOnly_
                                         ? &brls::BUTTONSTYLE_PRIMARY
                                         : &brls::BUTTONSTYLE_DEFAULT);
        recyclerHost_->setVisibility(count == 0 ? brls::Visibility::GONE
                                                : brls::Visibility::VISIBLE);
        recycler_->reloadData();
    }

    void toggleFocusedFavorite() {
        if (!favorites_)
            return;
        const std::vector<CatalogEntry>& roms = dataSource_->roms();
        const size_t row =
            static_cast<size_t>(recycler_->getDefaultFocusedIndex());
        const size_t index = row * grid::kColumns +
                             static_cast<size_t>(*focusColumn_);
        if (index >= roms.size())
            return;
        std::string error;
        favorites_->toggle(roms[index].infoHash, roms[index].title, error);
        if (!error.empty()) {
            brls::Application::notify(error);
            return;
        }
        rebuild();
    }

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
    // Solo para la ficha: es quien descarga y cachea portadas y capturas.
    GameMetadataService* metadata_;
    FavoritesService* favorites_;
    // La lista completa; lo que se ensena sale de filtrarla.
    std::vector<CatalogEntry> allRoms_;
    std::string summary_;
    bool summaryWarns_ = false;
    bool favoritesOnly_ = false;
    brls::Button* favoritesChip_ = nullptr;
    std::string destination_;
    brls::AppletFrame* frame_ = nullptr;
    brls::Label* pathLabel_ = nullptr;
    brls::Label* pathWarning_ = nullptr;
    brls::Label* status_ = nullptr;
    brls::RecyclerFrame* recycler_ = nullptr;
    brls::Box* recyclerHost_ = nullptr;
    RomDataSource* dataSource_ = nullptr;
    // Compartida con las tarjetas: recuerda en que columna estaba el foco al
    // cambiar de fila, igual que en el catalogo.
    std::shared_ptr<int> focusColumn_ = std::make_shared<int>(0);
    // La proporcion que habia antes de entrar, para devolverla al salir.
    float previousAspect_ = 0.0f;
    std::shared_ptr<std::atomic<bool>> alive_ =
        std::make_shared<std::atomic<bool>>(true);
};

inline GridCardInfo RomDataSource::makeInfo(size_t index) const {
    GridCardInfo info;
    info.entryIndex = static_cast<int>(index);
    const CatalogEntry& rom = roms_[index];
    info.infoHash = rom.infoHash;
    info.title = rom.title;
    info.iconUrl = rom.posterUrl;
    // Las portadas de consola son cajas verticales, no iconos cuadrados: sin
    // esto salen estiradas.
    info.iconPreserveAspect = true;
    info.favorite = favorites_ && favorites_->contains(rom.infoHash);

    // La linea de debajo dice lo que hay que saber ANTES de abrir: el tamano, o
    // el aviso cuando el emulador no va a poder con ello. El aviso gana, porque
    // el tamano no cambia la decision y esto si.
    const std::string file = gamehub::fileNameFromUrl(rom.directUrl);
    if (roms::displayCompat(rom.platformSlug, file) ==
        roms::RomCompat::Unplayable) {
        info.sub = roms::ticoSupportsPlatform(rom.platformSlug)
            ? upperExtension(roms::extensionOf(file))
            : tr("gamehubnx/platforms/platform_badge");
        info.subIsBadge = true;
    } else if (rom.size) {
        info.sub = formatBytes(rom.size);
    }
    return info;
}

inline brls::RecyclerCell* RomDataSource::cellForRow(
    brls::RecyclerFrame* recycler, brls::IndexPath index) {
    auto* cell =
        static_cast<GridRowCell*>(recycler->dequeueReusableCell("GridRow"));
    const size_t start = static_cast<size_t>(index.row) * grid::kColumns;
    std::vector<GridCardInfo> infos;
    for (size_t i = start;
         i < std::min(start + grid::kColumns, roms_.size()); ++i)
        infos.push_back(makeInfo(i));
    RomListActivity* owner = owner_;
    cell->setRow(infos, metadata_, [owner](int entryIndex) {
        // Abrir la ficha, no descargar. Antes un solo boton encolaba tres gigas
        // sin haber podido ver de que juego se trataba ni elegir edicion.
        if (entryIndex >= 0)
            owner->openDetail(static_cast<size_t>(entryIndex));
    });
    return cell;
}

} // namespace gamehubnx::ui
