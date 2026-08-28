#pragma once

#include <cctype>
#include <string>

#include <sys/stat.h>

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

// Cierto si la carpeta de ROMs de tico existe en la tarjeta.
//
// Se comprueba la CARPETA, no el .nro: lo que decide si vale la pena dejar ahi
// una ROM es que exista el arbol que tico lee, no que el ejecutable este en un
// sitio u otro. Alguien puede tener tico sin haberlo abierto nunca, o haberlo
// movido; la carpeta es la pregunta util.
inline bool ticoRomsPresent() {
    struct stat st {};
    return stat(kTicoRoot, &st) == 0 && S_ISDIR(st.st_mode);
}

// Extensiones que el EMULADOR sabe abrir, que no son las mismas que el
// servidor escanea.
//
// GameHub cataloga por la columna `extensions` de cada plataforma --lo que
// mete en la biblioteca-- y el emulador abre otro conjunto. En 3DS divergen de
// verdad: la biblioteca es casi toda .cia, que es un paquete de instalacion, y
// el emulador de tico abre .3ds/.cci/.cxi/.app y compañia. Descargar un .cia
// funciona, pero luego no aparece en el emulador y no hay ningun error que lo
// explique.
//
// Cadena vacia = no consta, y entonces NO se afirma nada: callarse es mejor que
// marcar como incompatible algo que quiza se abre perfectamente.
inline std::string playableExtensionsFor(const std::string& slug) {
    struct Pair { const char* slug; const char* exts; };
    static constexpr Pair kPlayable[] = {
        {"3ds", ".3ds .3dsx .z3dsx .elf .axf .cci .zcci .cxi .zcxi .app"},
    };
    for (const Pair& p : kPlayable)
        if (slug == p.slug)
            return p.exts;
    return {};
}

// Cierto cuando ese fichero NO esta entre lo que el emulador abre. Falso
// tambien cuando no consta la lista: sin dato no se acusa.
inline bool isUnplayableByEmulator(const std::string& slug,
                                   const std::string& fileName) {
    const std::string playable = playableExtensionsFor(slug);
    if (playable.empty())
        return false;
    const size_t dot = fileName.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    std::string ext = fileName.substr(dot);
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // Se busca la extension delimitada, para que ".cci" no case dentro de
    // ".zcci" ni ".3ds" dentro de ".3dsx".
    const std::string needle = ext + " ";
    return (playable + " ").find(needle) == std::string::npos;
}

} // namespace roms
} // namespace gamehubnx
