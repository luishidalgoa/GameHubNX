#pragma once

#include <string>

#include "app_paths.h"

namespace gamehubnx {

// Donde se guarda una ROM descargada.
//
// El destino por defecto apunta a tico (https://github.com/ticohq/tico), el
// frontend de emulacion de Switch: es lo que la mayoria de la gente tiene
// puesto, y dejar la ROM donde tico ya mira ahorra moverla a mano.
//
// tico NO lee una carpeta si no se llama exactamente como espera --hay un issue
// suyo sobre eso, ticohq/tico#148-- asi que estos nombres no se pueden
// improvisar: un nombre inventado no es un fallo estetico, es una ROM que se
// descarga y no aparece. Por eso la tabla de abajo solo lleva lo comprobado, y
// lo que no esta cae al slug del servidor y se corrige en Ajustes.
namespace roms {

// Raiz de tico en la SD.
inline constexpr const char* kTicoRoot = "sdmc:/tico/roms";

// Carpeta de tico para un slug de GameHub, o cadena vacia si no consta.
//
// Sin confirmar a proposito, y por eso ausentes:
//   nds   la guia de referencia manda la DS a MelonDS, en SD:/Roms/DS/, no a
//         tico. Son ~100 juegos, asi que no es un caso menor.
//   3ds   tico anadio 3DS via Azahar, pero no he encontrado el nombre de
//         carpeta.
//   psvita-ports  son .zip de homebrew portado, no ROMs: su sitio es
//         sdmc:/switch/, que es otro camino distinto.
inline std::string ticoFolderFor(const std::string& slug) {
    struct Pair { const char* slug; const char* folder; };
    static constexpr Pair kKnown[] = {
        {"gba", "gba"},
        {"super-nintendo-entertainment-system", "snes"},
        {"snes", "snes"},
        {"psp", "psp"},
        {"wii", "wii"},
        // Admitidas por tico aunque hoy no esten en el servidor: si aparecen
        // manana, funcionan solas.
        {"nes", "nes"},
        {"gb", "gb"},
        {"gbc", "gbc"},
        {"n64", "n64"},
        {"psx", "psx"},
        {"gamecube", "gc"},
        {"gc", "gc"},
        {"genesis", "genesis"},
        {"megadrive", "genesis"},
        {"master-system", "master-system"},
        {"game-gear", "game-gear"},
        {"sega-cd", "sega-cd"},
        {"dreamcast", "dc"},
        {"dc", "dc"},
    };
    for (const Pair& p : kKnown)
        if (slug == p.slug)
            return p.folder;
    return {};
}

// Ruta por defecto donde dejar las ROMs de una plataforma.
//
// Con carpeta conocida va a la de tico; sin ella se usa el slug tal cual, que
// al menos es estable y unico. En los dos casos el usuario puede cambiarla:
// quien no use tico no tiene por que aceptar su arbol.
inline std::string defaultRomPath(const std::string& slug) {
    const std::string folder = ticoFolderFor(slug);
    return std::string(kTicoRoot) + "/" + (folder.empty() ? slug : folder);
}

// Cierto cuando la carpeta sale de la tabla comprobada y no de un respaldo.
// La interfaz lo usa para avisar de que esa ruta hay que confirmarla.
inline bool defaultPathIsKnown(const std::string& slug) {
    return !ticoFolderFor(slug).empty();
}

} // namespace roms
} // namespace gamehubnx
