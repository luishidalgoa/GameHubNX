#pragma once

// UI: collapsible navigation frame.
//
// Wraps brls::TabFrame to (1) draw an icon next to every sidebar label and
// (2) fold the sidebar down to a slim icon rail while the user is browsing a
// tab's content, so the catalogue grid gets almost the whole screen. The rail
// re-expands the moment focus returns to the menu (B / left). No extra button:
// the fold is driven purely by focus.
//
// Everything lives here, in-tree — the vendored borealis submodule is left
// untouched. We reach the private sidebar bits we need through the public
// Sidebar::getItem() / Box::getChildren() surface.

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "app/download_manager.hpp"
#include "app/install_space.hpp"
#include "app/ftp_server.hpp"
#include "ui/common/storage_meter.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/common/ftp_address.hpp"
#include "ui/i18n.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui {

enum class NavIconType {
    Catalog, Platforms, Downloads, Installed, Updates, Files, Store, Settings, Help,
    About
};

// Alto que el pie anclado ocupa dentro de la columna, y por tanto lo que la
// lista de navegacion tiene que dejar libre. Sumando sus piezas (ver
// attachStorageFooter, mas abajo):
//
//   fila del servidor web   texto de 15px (~20) + margen inferior 8   =  28
//   medidor: cabecera       texto de 15px (~20) + margen 6            =  26
//   medidor: barra          14 + margen 2                             =  16
//   medidor: pie            texto de 15px (~20) + margen 6            =  26
//   relleno inferior del dock (kFooterPad)                            =  16
//                                                                       ---
//                                                                       112
//
// Se reservan 120. Pasarse deja hueco de mas al final de la lista; quedarse
// corto reproduce el solape.
//
// Va como MARGEN de la barra, no como relleno de su contenido: rellenar por
// dentro no mueve nada, porque la lista se dibuja de arriba abajo y la ultima
// entrada se queda donde estaba con mas hueco detras. Lo que hace falta es que
// la barra TERMINE antes; como es un ScrollingFrame, lo que no quepa se
// desplaza, que es exactamente lo que se quiere.
inline constexpr float kSidebarFooterReserve = 120.0f;

// Shrinks the stock sidebar so the icon rail + expanded menu both look right.
// Style metrics back a shared global table, so this must run once after
// Application::init() and BEFORE the first TabFrame/Sidebar is constructed
// (both read these at inflate time).
inline void installSidebarStyle() {
    brls::Style style = brls::Application::getStyle();
    style.addMetric("brls/tab_frame/sidebar_width", 248.0f);  // was 410
    style.addMetric("brls/sidebar/padding_left", 22.0f);      // was 80
    style.addMetric("brls/sidebar/padding_right", 16.0f);     // was 40
    style.addMetric("brls/sidebar/padding_top", 28.0f);
}

// Box that reports no hit at all, so touches fall through to whatever sits
// under it. setFocusable(false) is not enough: Box::hitTest claims any point
// inside its frame regardless, and unlike a focus request a touch does not
// walk on to the next sibling once a view has answered.
class TouchThroughBox : public brls::Box {
public:
    using brls::Box::Box;

    brls::View* hitTest(brls::Point) override { return nullptr; }
};

// Decorative glyph shown to the left of a sidebar label. Non-focusable; its
// colour tracks the owning item's active state so it lights up with the accent
// when its tab is selected — matching the label the sidebar already recolours.
class NavIcon : public brls::View {
public:
    NavIcon(NavIconType type, brls::SidebarItem* owner)
        : type_(type), owner_(owner) {
        this->setWidth(28.0f);
        this->setHeight(28.0f);
        this->setAlignSelf(brls::AlignSelf::CENTER);
        this->setFocusable(false);
        this->setMarginRight(12.0f);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override {
        const NVGcolor c = (owner_ && owner_->isActive())
                               ? theme::accent()
                               : theme::textSecondary();
        nvgStrokeColor(vg, c);
        nvgFillColor(vg, c);
        nvgStrokeWidth(vg, 2.0f);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);

        const float s = 24.0f;                    // glyph box side
        const float gx = x + (width - s) / 2.0f;  // glyph origin
        const float gy = y + (height - s) / 2.0f;
        switch (type_) {
            case NavIconType::Catalog:   drawCatalog(vg, gx, gy, s); break;
            case NavIconType::Platforms: drawPlatforms(vg, gx, gy, s); break;
            case NavIconType::Downloads: drawDownloads(vg, gx, gy, s); break;
            case NavIconType::Installed: drawInstalled(vg, gx, gy, s); break;
            case NavIconType::Updates:   drawUpdates(vg, gx, gy, s); break;
            case NavIconType::Files:     drawFiles(vg, gx, gy, s); break;
            case NavIconType::Store:     drawStore(vg, gx, gy, s); break;
            case NavIconType::Settings:    drawSettings(vg, gx, gy, s); break;
            case NavIconType::Help:        drawPulse(vg, gx, gy, s); break;
            case NavIconType::About:       drawAbout(vg, gx, gy, s); break;
        }
    }

private:
    // Mando: cuerpo redondeado con cruceta y dos botones. El catalogo de Switch
    // es una rejilla de cajas; esto tenia que leerse distinto de un vistazo en
    // la barra, y una consola es lo que estas plataformas son.
    // Escaparate: toldo con festones sobre una fachada con puerta. Es lo que
    // se ve al entrar en una tienda; la app de tiendas lo lleva en la barra.
    static void drawStore(NVGcontext* vg, float gx, float gy, float s) {
        const float awningY = gy + s * 0.30f;
        nvgBeginPath(vg);
        nvgMoveTo(vg, gx, awningY);
        nvgLineTo(vg, gx + s, awningY);
        // Festones: cuatro arcos bajo el toldo.
        const float scallop = s / 4.0f;
        for (int i = 0; i < 4; ++i) {
            const float x0 = gx + scallop * i;
            nvgLineTo(vg, x0 + scallop, awningY);
            nvgQuadTo(vg, x0 + scallop / 2.0f, awningY + s * 0.16f, x0, awningY);
        }
        nvgStroke(vg);

        nvgBeginPath(vg);
        nvgRect(vg, gx + s * 0.12f, awningY + s * 0.04f, s * 0.76f, s * 0.56f);
        nvgStroke(vg);

        nvgBeginPath(vg);
        nvgRect(vg, gx + s * 0.40f, awningY + s * 0.30f, s * 0.20f, s * 0.30f);
        nvgStroke(vg);
    }

    static void drawPlatforms(NVGcontext* vg, float gx, float gy, float s) {
        const float top = gy + s * 0.24f;
        const float h = s * 0.52f;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, gx, top, s, h, h / 2.0f);
        nvgStroke(vg);

        const float cy = top + h / 2.0f;
        const float arm = s * 0.13f;
        const float lx = gx + s * 0.27f;
        nvgBeginPath(vg);
        nvgMoveTo(vg, lx - arm, cy);
        nvgLineTo(vg, lx + arm, cy);
        nvgMoveTo(vg, lx, cy - arm);
        nvgLineTo(vg, lx, cy + arm);
        nvgStroke(vg);

        // Los botones van macizos: a tamano de barra un circulo de contorno de
        // 2px de radio no se distingue de un punto sucio.
        const float r = s * 0.075f;
        for (int i = 0; i < 2; ++i) {
            nvgBeginPath(vg);
            nvgCircle(vg, gx + s * (0.70f + 0.14f * i), cy, r);
            nvgFill(vg);
        }
    }

    // 2x2 grid of rounded squares.
    static void drawCatalog(NVGcontext* vg, float gx, float gy, float s) {
        const float cell = 9.0f;
        const float step = s - cell;  // 15 -> 6px gap
        for (int i = 0; i < 4; i++) {
            const float px = gx + (i % 2) * step;
            const float py = gy + (i / 2) * step;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, px, py, cell, cell, 2.0f);
            nvgStroke(vg);
        }
    }

    // Down arrow dropping into a tray.
    static void drawDownloads(NVGcontext* vg, float gx, float gy, float s) {
        const float cx = gx + s / 2.0f;
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx, gy + 1.0f);
        nvgLineTo(vg, cx, gy + 14.0f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 5.0f, gy + 9.0f);
        nvgLineTo(vg, cx, gy + 14.0f);
        nvgLineTo(vg, cx + 5.0f, gy + 9.0f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, gx + 3.0f, gy + 15.0f);
        nvgLineTo(vg, gx + 3.0f, gy + 21.0f);
        nvgLineTo(vg, gx + s - 3.0f, gy + 21.0f);
        nvgLineTo(vg, gx + s - 3.0f, gy + 15.0f);
        nvgStroke(vg);
    }

    // Rounded square with a checkmark.
    static void drawInstalled(NVGcontext* vg, float gx, float gy, float s) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, gx + 1.0f, gy + 1.0f, s - 2.0f, s - 2.0f, 4.0f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, gx + 6.0f, gy + 12.0f);
        nvgLineTo(vg, gx + 10.0f, gy + 16.0f);
        nvgLineTo(vg, gx + 17.0f, gy + 8.0f);
        nvgStroke(vg);
    }

    // Circular update arrow.
    static void drawUpdates(NVGcontext* vg, float gx, float gy, float s) {
        const float cx = gx + s / 2.0f;
        const float cy = gy + s / 2.0f;
        const float r = s / 2.0f - 3.0f;
        nvgBeginPath(vg);
        nvgArc(vg, cx, cy, r, -0.35f, 4.7f, NVG_CW);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx + r - 1.0f, cy - 4.0f);
        nvgLineTo(vg, cx + r + 3.0f, cy - 10.0f);
        nvgLineTo(vg, cx + r + 5.0f, cy - 2.0f);
        nvgStroke(vg);
    }

    // Three fader lines with offset knobs.
    // Carpeta. Es el glifo que todo el mundo asocia a "ficheros", y a tamano de
    // barra un dibujo mas literal --hojas, listas-- se convierte en manchas.
    static void drawFiles(NVGcontext* vg, float gx, float gy, float s) {
        const float top = gy + s * 0.26f;
        const float h = s * 0.50f;
        const float r = s * 0.08f;
        // La pestana de la carpeta: sube desde la esquina izquierda y baja a
        // media anchura, que es lo que la distingue de una caja cualquiera.
        nvgBeginPath(vg);
        nvgMoveTo(vg, gx + r, top);
        nvgLineTo(vg, gx + s * 0.34f, top);
        nvgLineTo(vg, gx + s * 0.44f, top + s * 0.10f);
        nvgLineTo(vg, gx + s - r, top + s * 0.10f);
        nvgArcTo(vg, gx + s, top + s * 0.10f, gx + s, top + s * 0.10f + r, r);
        nvgLineTo(vg, gx + s, top + h - r);
        nvgArcTo(vg, gx + s, top + h, gx + s - r, top + h, r);
        nvgLineTo(vg, gx + r, top + h);
        nvgArcTo(vg, gx, top + h, gx, top + h - r, r);
        nvgLineTo(vg, gx, top + r);
        nvgArcTo(vg, gx, top, gx + r, top, r);
        nvgClosePath(vg);
        nvgStroke(vg);
    }

    static void drawSettings(NVGcontext* vg, float gx, float gy, float s) {
        const float ys[3] = {gy + 5.0f, gy + 12.0f, gy + 19.0f};
        const float knob[3] = {gx + 8.0f, gx + 16.0f, gx + 11.0f};
        for (int i = 0; i < 3; i++) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, gx + 2.0f, ys[i]);
            nvgLineTo(vg, gx + s - 2.0f, ys[i]);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgCircle(vg, knob[i], ys[i], 2.6f);
            nvgFill(vg);
        }
    }

    // Info circle: dot over a stem.
    static void drawAbout(NVGcontext* vg, float gx, float gy, float s) {
        const float cx = gx + s / 2.0f;
        const float cy = gy + s / 2.0f;
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, s / 2.0f - 1.0f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, cx, gy + 7.0f, 1.3f);
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx, gy + 11.0f);
        nvgLineTo(vg, cx, gy + 17.0f);
        nvgStroke(vg);
    }

    // ECG pulse: a flat trace with one sharp spike — diagnostics.
    static void drawPulse(NVGcontext* vg, float gx, float gy, float s) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, gx + 2.0f, gy + 15.0f);
        nvgLineTo(vg, gx + 9.0f, gy + 15.0f);
        nvgLineTo(vg, gx + 11.0f, gy + 7.0f);
        nvgLineTo(vg, gx + 13.0f, gy + 19.0f);
        nvgLineTo(vg, gx + 15.0f, gy + 15.0f);
        nvgLineTo(vg, gx + s - 2.0f, gy + 15.0f);
        nvgStroke(vg);
    }

    NavIconType type_;
    brls::SidebarItem* owner_;
};

// Una linea de estado del FTP en el pie de la barra lateral: un punto (acento
// = sirviendo, apagado = no) y la direccion a la que conectarse. No enfocable:
// es un lectura, no un control.
class FtpStatusRow : public brls::Box {
public:
    FtpStatusRow() : brls::Box(brls::Axis::ROW) {
        setFocusable(false);
        setAlignItems(brls::AlignItems::CENTER);
        setMarginBottom(8);
        setClipsToBounds(true);
        dot_ = new Dot();
        addView(dot_);
        label_ = new brls::Label();
        label_->setSingleLine(true);
        label_->setFontSize(theme::kFontCaption);
        label_->setTextColor(theme::textSecondary());
        addView(label_);
    }

    void setState(bool running, const std::string& url) {
        dot_->running = running;
        if (!running) {
            setTextIfChanged(label_, tr("gamehubnx/ftp/off"));
            label_->setTextColor(theme::textTertiary());
        } else if (url.empty()) {
            setTextIfChanged(label_, tr("gamehubnx/ftp/no_address"));
            label_->setTextColor(theme::textTertiary());
        } else {
            // Sin el esquema: la columna del pie mide 216px y cada pixel
            // cuenta.
            setTextIfChanged(label_, url.rfind("ftp://", 0) == 0
                                         ? url.substr(6)
                                         : url);
            label_->setTextColor(theme::textSecondary());
        }
    }

private:
    class Dot : public brls::View {
    public:
        Dot() {
            setWidth(10);
            setHeight(10);
            setMarginRight(8);
            setFocusable(false);
            setAlignSelf(brls::AlignSelf::CENTER);
        }
        void draw(NVGcontext* vg, float x, float y, float width, float height,
                  brls::Style, brls::FrameContext*) override {
            nvgBeginPath(vg);
            nvgCircle(vg, x + width / 2.0f, y + height / 2.0f, 4.0f);
            nvgFillColor(vg, running ? theme::accent()
                                     : theme::textTertiary());
            nvgFill(vg);
        }
        bool running = false;
    };

    Dot* dot_ = nullptr;
    brls::Label* label_ = nullptr;
};

// TabFrame that carries icons and folds to an icon rail while a tab is focused.
class MainFrame : public brls::TabFrame {
public:
    MainFrame() {
        expandedWidth_ =
            brls::Application::getStyle()["brls/tab_frame/sidebar_width"];
        if (expandedWidth_ < 1.0f)
            expandedWidth_ = 248.0f;
    }

    // Like TabFrame::addTab, but also plants an icon between the active-accent
    // bar and the label, and remembers the label so it can be folded away.
    void addNavTab(const std::string& label, NavIconType icon,
                   brls::TabViewCreator creator) {
        this->addTab(label, std::move(creator));
        const int index = static_cast<int>(this->sidebar->getItemsSize()) - 1;
        brls::SidebarItem* item = this->sidebar->getItem(index);
        if (!item)
            return;

        // Item children start as [accent, label]; capture the label before we
        // splice the icon in at index 1 -> [accent, icon, label].
        std::vector<brls::View*>& kids = item->getChildren();
        brls::View* labelView = kids.size() >= 2 ? kids[1] : nullptr;
        item->addView(new NavIcon(icon, item), 1);
        if (labelView) {
            labels_.push_back(labelView);
            if (collapsed_)
                labelView->setVisibility(brls::Visibility::GONE);
        }
    }

    void setCollapsed(bool collapsed) {
        if (collapsed == collapsed_)
            return;
        collapsed_ = collapsed;
        this->sidebar->setWidth(collapsed ? kCollapsedWidth : expandedWidth_);
        for (brls::View* label : labels_)
            label->setVisibility(collapsed ? brls::Visibility::GONE
                                           : brls::Visibility::VISIBLE);
        if (dock_) {
            dock_->setWidth(collapsed ? kCollapsedWidth : expandedWidth_);
            footer_->setCompact(collapsed);
            // The rail is 88px — no room for an address line.
            if (webRow_)
                webRow_->setVisibility(collapsed ? brls::Visibility::GONE
                                                 : brls::Visibility::VISIBLE);
        }
    }

    // Free-space readout pinned to the bottom-left of the frame, over the
    // sidebar area. Absolute + non-focusable so it never disturbs the sidebar's
    // scroll or the focus-driven fold logic.
    //
    // The meter itself is *not* the absolute node: an absolute box with only
    // left/bottom set forces Yoga to solve its top edge from a height that
    // comes out of a Label measure func, which put the widget below the content
    // area with nothing to clip it. The dock pins top+bottom instead, so its
    // height is definite, justifyContent flex-end parks the meter on the floor
    // whatever it measures, and clipsToBounds is the backstop.
    void attachStorageFooter(DownloadManager* manager,
                             gamehubnx::FtpServer* ftpServer = nullptr) {
        manager_ = manager;
        ftpServer_ = ftpServer;
        // Pinned over the full height of the sidebar column, so it has to be
        // touch-transparent or it eats every tap aimed at the tab items.
        dock_ = new TouchThroughBox(brls::Axis::COLUMN);
        dock_->setFocusable(false);
        dock_->setPositionType(brls::PositionType::ABSOLUTE);
        dock_->setPositionTop(0);
        dock_->setPositionBottom(0);
        dock_->setPositionLeft(0);
        dock_->setWidth(collapsed_ ? kCollapsedWidth : expandedWidth_);
        dock_->setJustifyContent(brls::JustifyContent::FLEX_END);
        dock_->setPadding(0, kFooterPad, kFooterPad, kFooterPad);
        dock_->setClipsToBounds(true);

        // La barra termina donde empieza el pie. Sin esto ocupa toda la
        // columna y el pie --que va por encima, en absoluto-- se le monta:
        // con siete entradas, "Acerca de" queda debajo de la IP.
        //
        // OJO al verificarlo: el arnes golden NO ve este cambio. El fondo de la
        // barra es rgb(50,50,50) y el de la app rgb(45,45,45), cinco de
        // diferencia por canal, y con fuzz del 5% cuentan como el mismo color.
        // Recortar 120 px de barra mueve ~30.000 pixeles y la comparacion los
        // da por identicos. Para comprobarlo hay que medir donde termina ese
        // fondo, no fiarse del veredicto de golden ni del ojo.
        this->sidebar->setMarginBottom(kSidebarFooterReserve);

        webRow_ = new FtpStatusRow();
        webRow_->setVisibility(collapsed_ ? brls::Visibility::GONE
                                          : brls::Visibility::VISIBLE);
        dock_->addView(webRow_);
        footer_ = new StorageMeter();
        footer_->setCompact(collapsed_);
        dock_->addView(footer_);
        addView(dock_);
        refreshStorage();
        refreshFtpStatus();
        // The periodic refresh runs off a timer + brls::async, NOT from
        // draw(): nsGetStorageSize and nifmGetCurrentIpAddress are
        // synchronous service IPC and used to stall the frame being
        // recorded every 2 seconds.
        queryTimer_.setCallback([this] { scheduleRefresh(); });
        queryTimer_.start(2000);
    }

    ~MainFrame() override {
        queryTimer_.stop();
        alive_->store(false);
    }

protected:
    // Focus in the sidebar -> expanded menu; focus in a tab's content -> icon
    // rail. Both subtrees are direct children of this frame, so this fires on
    // every menu<->content crossing.
    void onChildFocusGained(brls::View* directChild,
                            brls::View* focusedView) override {
        brls::TabFrame::onChildFocusGained(directChild, focusedView);
        setCollapsed(!(this->sidebar == directChild));
    }

private:
    void scheduleRefresh() {
        if (!footer_ || !manager_ || queryInFlight_)
            return;
        queryInFlight_ = true;
        auto alive = alive_;
        std::string root = manager_->rootPath();
        gamehubnx::FtpServer* server = ftpServer_;
        brls::async([this, alive, root, server] {
            const gamehubnx::StorageSpaceSnapshot storage =
                gamehubnx::queryStorageSpace(root);
            const bool running = server ? server->running() : true;
            std::string url = running ? ftpAddress(server, true) : "";
            brls::sync([this, alive, storage, running, url = std::move(url)] {
                if (!alive->load())
                    return;
                queryInFlight_ = false;
                if (storage.available)
                    footer_->setStorage(storage.totalBytes, storage.freeBytes);
                else
                    footer_->setUnavailable();
                if (webRow_)
                    webRow_->setState(running, url);
            });
        });
    }

    void refreshStorage() {
        if (!footer_ || !manager_)
            return;
        const gamehubnx::StorageSpaceSnapshot storage =
            gamehubnx::queryStorageSpace(manager_->rootPath());
        if (storage.available)
            footer_->setStorage(storage.totalBytes, storage.freeBytes);
        else
            footer_->setUnavailable();
    }

    void refreshFtpStatus() {
        if (!webRow_)
            return;
        // A null server (golden runner) reads as "serving on the fixed fake
        // address" so the baseline row looks like the real thing.
        const bool running = ftpServer_ ? ftpServer_->running() : true;
        webRow_->setState(running, running ? ftpAddress(ftpServer_, true) : "");
    }

    // Wide enough for padding + the active-accent bar + the 28px icon.
    static constexpr float kCollapsedWidth = 88.0f;
    static constexpr float kFooterPad = 16.0f;

    bool collapsed_ = false;
    float expandedWidth_ = 248.0f;
    std::vector<brls::View*> labels_;
    brls::Box* dock_ = nullptr;
    StorageMeter* footer_ = nullptr;
    FtpStatusRow* webRow_ = nullptr;
    DownloadManager* manager_ = nullptr;
    gamehubnx::FtpServer* ftpServer_ = nullptr;
    brls::RepeatingTimer queryTimer_;
    bool queryInFlight_ = false;
    std::shared_ptr<std::atomic<bool>> alive_ =
        std::make_shared<std::atomic<bool>>(true);
};

}  // namespace gamehubnx::ui
