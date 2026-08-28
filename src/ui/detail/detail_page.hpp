#pragma once

// La maqueta de una ficha de juego, compartida por Switch y por las ROMs.
//
// Nace de un fallo real: la ficha de ROM REPLICABA a la de Switch en vez de
// reutilizarla, y las dos se descuadraron enseguida --portada con bandas, el
// recuadro de foco encima del texto, el carril como callejon sin salida--.
// Copiar medidas a mano garantiza que se separen; compartirlas garantiza que
// no.
//
// Aqui vive SOLO lo que las dos pantallas tienen en comun: las dos columnas,
// la portada, la tabla de datos y el carril de capturas. Lo que no comparten
// --instalar, complementos, medidor de almacenamiento, favoritos-- se queda en
// cada una. Meter eso aqui obligaria a que la ficha de una ROM cargase con
// media maquinaria de instalacion de Switch para no usarla.

#include <string>
#include <vector>

#include <borealis.hpp>

#include "app/game_metadata_service.hpp"
#include "ui/common/async_image.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/detail/screenshot_viewer.hpp"
#include "ui/i18n.hpp"
#include "ui/theme.hpp"

namespace gamehubnx::ui::detail_page {

// La columna izquierda no tiene scroll, asi que su alto es un presupuesto
// duro: 720 menos la cabecera del marco (88), menos el pie (73), menos el
// relleno de la pagina (24 arriba y abajo) deja 511 px. Lo que se pase de ahi
// se dibuja ENCIMA de la barra de abajo, porque Yoga no recorta por su cuenta.
inline constexpr float kLeftColumnWidth = 320.0f;
inline constexpr float kCoverHeight = 200.0f;

// Alto del carril de capturas. Es mayor que la imagen a proposito: el recuadro
// de foco se dibuja alrededor y sin ese hueco se sale y pisa el texto de al
// lado.
inline constexpr float kRailHeight = 180.0f;
inline constexpr float kShotWidth = 300.0f;
inline constexpr float kShotHeight = 170.0f;

// Columna izquierda vacia, con su ancho y su recorte. El recorte es la red de
// seguridad del presupuesto de arriba: un texto de error que se envuelva en
// mas lineas de las previstas se corta en vez de invadir el pie.
inline brls::Box* makeLeftColumn() {
    auto* left = new brls::Box(brls::Axis::COLUMN);
    left->setWidth(kLeftColumnWidth);
    left->setMarginRight(32);
    left->setClipsToBounds(true);
    return left;
}

// La portada, con el ancho sacado de la proporcion en vigor y centrada en la
// columna.
//
// FIT deja bandas donde no llega la imagen; con el recorte activado borealis
// las pinta DENTRO de la vista y se ven como marcos negros. Por eso va
// desactivado, y por eso una caja de 320x320 con una portada vertical dentro
// --lo que hacia la ficha de ROM-- salia con dos franjas enormes.
inline AsyncRgbaImage* makeCover(brls::Box* left) {
    const float width = std::min(kLeftColumnWidth, kCoverHeight * coverAspect());
    auto* cover = new AsyncRgbaImage();
    cover->setWidth(width);
    cover->setHeight(kCoverHeight);
    cover->setMarginLeft((kLeftColumnWidth - width) / 2.0f);
    cover->setCornerRadius(theme::kRadiusLarge);
    cover->setScalingType(brls::ImageScalingType::FIT);
    cover->setClipsToBounds(false);
    left->addView(cover);
    return cover;
}

// Una fila de la tabla de datos: etiqueta a la izquierda, valor a la derecha.
// Un valor vacio no pinta fila -- una etiqueta sin dato ocupa sitio y no dice
// nada.
inline void addFactRow(brls::Box* table, const std::string& name,
                       const std::string& value) {
    if (value.empty())
        return;
    auto* row = new brls::Box(brls::Axis::ROW);
    row->setMarginTop(8);
    auto* key = new brls::Label();
    key->setFontSize(theme::kFontCaption);
    key->setTextColor(theme::textTertiary());
    key->setWidth(160);
    key->setText(name);
    row->addView(key);
    auto* val = new brls::Label();
    val->setFontSize(theme::kFontCaption);
    val->setTextColor(theme::textSecondary());
    val->setGrow(1);
    val->setText(value);
    row->addView(val);
    table->addView(row);
}

// El carril de capturas, con su cabecera.
//
// `upTarget` es a donde va el foco al pulsar ARRIBA desde una miniatura. Sin
// el, el carril es un callejon: la columna derecha no tiene nada mas enfocable,
// el paseo del foco choca con el eje horizontal del contenedor --que ignora
// ARRIBA-- y muere ahi. Puede ser nulo cuando de verdad no hay a donde ir, y
// entonces no se enruta nada en vez de apuntar a algo que no toma el foco.
inline void addScreenshotRail(brls::Box* right, GameMetadataService* metadata,
                              const std::vector<std::string>& shots,
                              const std::string& viewerTitle,
                              brls::View* upTarget) {
    if (shots.empty() || !metadata)
        return;
    auto* header = new brls::Label();
    header->setFontSize(theme::kFontSmall);
    header->setMarginTop(24);
    header->setMarginBottom(8);
    header->setTextColor(theme::textSecondary());
    header->setText(tr("gamehubnx/detail/screenshots"));
    right->addView(header);

    auto* rail = new brls::Box(brls::Axis::ROW);
    rail->setHeight(kRailHeight);
    for (size_t i = 0; i < shots.size(); ++i) {
        auto* image = new AsyncRgbaImage();
        image->setWidth(kShotWidth);
        image->setHeight(kShotHeight);
        image->setMarginRight(12);
        image->setCornerRadius(theme::kRadiusSmall);
        image->setFocusable(true);
        image->setScalingType(brls::ImageScalingType::FIT);
        image->setClipsToBounds(false);
        if (upTarget)
            image->setCustomNavigationRoute(brls::FocusDirection::UP,
                                            upTarget);
        image->registerClickAction(
            [metadata, shots, i, viewerTitle](brls::View*) {
                brls::Application::pushActivity(
                    new ScreenshotViewerActivity(metadata, shots, i,
                                                 viewerTitle));
                return true;
            });
        rail->addView(image);
        loadImageInto(image, metadata, shots[i]);
    }
    right->addView(rail);
}

// Columna derecha, con scroll. La izquierda no lo lleva a proposito: sus
// botones tienen que estar siempre a la vista.
inline brls::Box* makeRightColumn(brls::Box* columns) {
    auto* right = new brls::Box(brls::Axis::COLUMN);
    right->setPadding(0, 12, 24, 0);
    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1);
    scroll->setContentView(right);
    columns->addView(scroll);
    return right;
}

// El contenedor de las dos columnas, con el relleno que asume el presupuesto
// de la columna izquierda.
inline brls::Box* makeColumns() {
    auto* columns = new brls::Box(brls::Axis::ROW);
    columns->setGrow(1);
    columns->setPadding(24, 32, 24, 32);
    return columns;
}

} // namespace gamehubnx::ui::detail_page
