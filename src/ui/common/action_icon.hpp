#pragma once

// Row-state glyphs for the pre-install selection lists (torrent file selection,
// batch install). Hand-drawn with NanoVG rather than an icon font: the Material
// fallback is registered per platform (desktop_font.cpp vs switch_font.cpp), so
// a font glyph would not be guaranteed identical between the Switch build and
// the golden-screenshot harness. Same drawing conventions as NavIcon in
// ui/main_frame.hpp — 24px glyph box inside a 28px view, 2px round stroke,
// colour resolved at draw time so it follows the light/dark theme.
//
// The three selection states are separated by FILL WEIGHT, not by glyph detail:
// at row scale a solid badge, an outline glyph and a thin hollow ring are
// told apart instantly, whereas two similar arrows are not.

#include <algorithm>

#include <borealis.hpp>

#include "ui/theme.hpp"

namespace gamehubnx::ui {

enum class ActionIconKind {
    Install,    // solid accent badge, arrow knocked out
    Download,   // outline arrow dropping into a tray
    Skip,       // thin hollow ring, dimmed
    Checked,    // solid accent badge, checkmark knocked out
    Unchecked,  // thin hollow ring, dimmed
    // Casilla vacia CUADRADA. La redonda choca con el anillo del glifo de DLC:
    // puestos uno al lado del otro en la misma fila se leen como dos circulos
    // y hay que pararse a mirar cual es cual. Se ve en el render de "que
    // instalar", no en un mockup.
    UncheckedBox,
    Error,      // outline warning triangle
    // Tipo de contenido, en la lista de "que instalar". Mismo sistema que los
    // tres de arriba: se distinguen por PESO DE RELLENO, no por detalle, que a
    // tamano de fila es lo unico que se lee sin fijarse.
    Game,       // cartucho macizo: la pieza completa
    Update,     // caja de contorno con flecha arriba: modifica algo que ya hay
    Dlc,        // anillo fino con un mas: un anadido
};

class ActionIcon : public brls::View {
public:
    static constexpr float kSize = 28.0f;

    explicit ActionIcon(ActionIconKind kind = ActionIconKind::Skip,
                        float size = kSize)
        : kind_(kind) {
        setWidth(size);
        setHeight(size);
        setAlignSelf(brls::AlignSelf::CENTER);
        setFocusable(false);
    }

    void setKind(ActionIconKind kind) { kind_ = kind; }
    ActionIconKind kind() const { return kind_; }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style, brls::FrameContext*) override {
        // Every glyph below is authored in a fixed 24-unit box; the transform
        // maps it onto whatever size the view was given, so the same paths
        // serve both the 28px row icons and the smaller legend swatches.
        const float side = std::min(width, height);
        const float scale = side / kGlyph;

        nvgSave(vg);
        nvgTranslate(vg, x + (width - side) / 2.0f, y + (height - side) / 2.0f);
        nvgScale(vg, scale, scale);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);

        switch (kind_) {
            case ActionIconKind::Install:
                drawBadge(vg, 0.0f, 0.0f, kGlyph, theme::accent());
                knockOutArrow(vg, 0.0f, 0.0f, kGlyph);
                break;
            case ActionIconKind::Download:
                drawTrayArrow(vg, 0.0f, 0.0f, kGlyph, theme::accent());
                break;
            case ActionIconKind::Skip:
            case ActionIconKind::Unchecked:
                drawRing(vg, 0.0f, 0.0f, kGlyph, theme::textTertiary());
                break;
            case ActionIconKind::Checked:
                drawBadge(vg, 0.0f, 0.0f, kGlyph, theme::accent());
                knockOutCheck(vg, 0.0f, 0.0f, kGlyph);
                break;
            case ActionIconKind::Error:
                drawWarning(vg, 0.0f, 0.0f, kGlyph, theme::error());
                break;
            case ActionIconKind::Game:
                drawCartridge(vg, 0.0f, 0.0f, kGlyph);
                break;
            case ActionIconKind::Update:
                drawUpdateBox(vg, 0.0f, 0.0f, kGlyph, theme::warning());
                break;
            case ActionIconKind::Dlc:
                drawRingPlus(vg, 0.0f, 0.0f, kGlyph, theme::textSecondary());
                break;
            case ActionIconKind::UncheckedBox:
                drawEmptyBox(vg, 0.0f, 0.0f, kGlyph, theme::textTertiary());
                break;
        }
        nvgRestore(vg);
    }

private:
    static constexpr float kGlyph = 24.0f;  // authoring box for every path

    // Cartucho MACIZO con la muesca troquelada: silueta llena para el juego
    // base, que es la pieza completa.
    static void drawCartridge(NVGcontext* vg, float gx, float gy, float s) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, gx + 6.0f, gy + 3.0f);
        nvgLineTo(vg, gx + 15.0f, gy + 3.0f);
        nvgLineTo(vg, gx + 18.0f, gy + 6.0f);
        nvgLineTo(vg, gx + 18.0f, gy + s - 3.0f);
        nvgLineTo(vg, gx + 6.0f, gy + s - 3.0f);
        nvgClosePath(vg);
        nvgFillColor(vg, theme::textPrimary());
        nvgFill(vg);
        // La ranura se troquela con el color del fondo, no con otro trazo: a
        // tamano de fila un trazo mas solo ensucia la silueta.
        nvgBeginPath(vg);
        nvgRoundedRect(vg, gx + 9.0f, gy + 6.5f, 6.0f, 2.4f, 1.2f);
        nvgFillColor(vg, theme::color(theme::Tok::Surface));
        nvgFill(vg);
    }

    // Caja de CONTORNO con flecha arriba: peso intermedio.
    static void drawUpdateBox(NVGcontext* vg, float gx, float gy, float s,
                              NVGcolor color) {
        nvgStrokeColor(vg, color);
        nvgStrokeWidth(vg, 2.0f);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, gx + 3.0f, gy + 3.0f, s - 6.0f, s - 6.0f, 5.0f);
        nvgStroke(vg);
        const float cx = gx + s / 2.0f;
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx, gy + 16.5f);
        nvgLineTo(vg, cx, gy + 8.0f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 3.4f, gy + 11.4f);
        nvgLineTo(vg, cx, gy + 8.0f);
        nvgLineTo(vg, cx + 3.4f, gy + 11.4f);
        nvgStroke(vg);
    }

    // Casilla vacia: mismo cuadrado redondeado que la placa marcada, en
    // contorno. Que la marcada y la vacia compartan silueta es lo que hace
    // que una columna de casillas se lea de un vistazo.
    static void drawEmptyBox(NVGcontext* vg, float gx, float gy, float s,
                             NVGcolor color) {
        nvgStrokeColor(vg, color);
        nvgStrokeWidth(vg, 1.8f);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, gx + 3.0f, gy + 3.0f, s - 6.0f, s - 6.0f,
                       theme::kRadiusSmall - 1.0f);
        nvgStroke(vg);
    }

    // Anillo FINO con un mas: el peso mas ligero de los tres.
    static void drawRingPlus(NVGcontext* vg, float gx, float gy, float s,
                             NVGcolor color) {
        const float cx = gx + s / 2.0f;
        const float cy = gy + s / 2.0f;
        nvgStrokeColor(vg, color);
        nvgStrokeWidth(vg, 1.6f);
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 9.0f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx, cy - 3.8f);
        nvgLineTo(vg, cx, cy + 3.8f);
        nvgMoveTo(vg, cx - 3.8f, cy);
        nvgLineTo(vg, cx + 3.8f, cy);
        nvgStroke(vg);
    }

    // Filled plate the knocked-out glyphs are cut into.
    static void drawBadge(NVGcontext* vg, float gx, float gy, float s,
                          NVGcolor color) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, gx + 1.0f, gy + 1.0f, s - 2.0f, s - 2.0f,
                       theme::kRadiusSmall);
        nvgFillColor(vg, color);
        nvgFill(vg);
    }

    // Down arrow landing on a baseline, stroked in the on-accent ink so it
    // reads as a hole in the badge on either theme.
    static void knockOutArrow(NVGcontext* vg, float gx, float gy, float s) {
        const float cx = gx + s / 2.0f;
        nvgStrokeColor(vg, theme::onAccent());
        nvgStrokeWidth(vg, 2.2f);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx, gy + 6.0f);
        nvgLineTo(vg, cx, gy + 14.0f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 4.0f, gy + 10.0f);
        nvgLineTo(vg, cx, gy + 14.5f);
        nvgLineTo(vg, cx + 4.0f, gy + 10.0f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, gx + 7.0f, gy + 17.5f);
        nvgLineTo(vg, gx + s - 7.0f, gy + 17.5f);
        nvgStroke(vg);
    }

    static void knockOutCheck(NVGcontext* vg, float gx, float gy, float s) {
        nvgStrokeColor(vg, theme::onAccent());
        nvgStrokeWidth(vg, 2.4f);
        nvgBeginPath(vg);
        nvgMoveTo(vg, gx + 6.5f, gy + 12.0f);
        nvgLineTo(vg, gx + 10.5f, gy + 16.0f);
        nvgLineTo(vg, gx + 17.5f, gy + 8.0f);
        nvgStroke(vg);
    }

    // Same shape as the Downloads tab icon, so "download only" reads the same
    // everywhere in the app.
    static void drawTrayArrow(NVGcontext* vg, float gx, float gy, float s,
                              NVGcolor color) {
        const float cx = gx + s / 2.0f;
        nvgStrokeColor(vg, color);
        nvgStrokeWidth(vg, 2.0f);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx, gy + 2.0f);
        nvgLineTo(vg, cx, gy + 13.0f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 4.5f, gy + 8.5f);
        nvgLineTo(vg, cx, gy + 13.5f);
        nvgLineTo(vg, cx + 4.5f, gy + 8.5f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, gx + 3.5f, gy + 15.0f);
        nvgLineTo(vg, gx + 3.5f, gy + 20.5f);
        nvgLineTo(vg, gx + s - 3.5f, gy + 20.5f);
        nvgLineTo(vg, gx + s - 3.5f, gy + 15.0f);
        nvgStroke(vg);
    }

    static void drawRing(NVGcontext* vg, float gx, float gy, float s,
                         NVGcolor color) {
        nvgStrokeColor(vg, color);
        nvgStrokeWidth(vg, 1.6f);
        nvgBeginPath(vg);
        nvgCircle(vg, gx + s / 2.0f, gy + s / 2.0f, s / 2.0f - 3.0f);
        nvgStroke(vg);
    }

    static void drawWarning(NVGcontext* vg, float gx, float gy, float s,
                            NVGcolor color) {
        const float cx = gx + s / 2.0f;
        nvgStrokeColor(vg, color);
        nvgFillColor(vg, color);
        nvgStrokeWidth(vg, 2.0f);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx, gy + 3.0f);
        nvgLineTo(vg, gx + s - 2.0f, gy + s - 4.0f);
        nvgLineTo(vg, gx + 2.0f, gy + s - 4.0f);
        nvgClosePath(vg);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx, gy + 9.0f);
        nvgLineTo(vg, cx, gy + 14.0f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, cx, gy + 17.5f, 1.3f);
        nvgFill(vg);
    }

    ActionIconKind kind_;
};

}  // namespace gamehubnx::ui
