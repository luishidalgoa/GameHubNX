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

// Que abre de verdad el emulador, que no es lo mismo que el servidor escanea.
//
// GameHub cataloga por su columna `extensions` --lo que mete en la
// biblioteca-- y el emulador abre otro conjunto. Casi siempre coinciden, y
// cuando no coinciden no salta ningun error: la ROM se descarga entera, se
// deja en su carpeta y sencillamente no aparece en tico.
//
// De donde salen estas listas. tico NO trae nucleos --lo dice su propio
// README: "does not include ... emulator cores"-- sino que ejecuta nucleos
// libretro y emuladores sueltos. Asi que lo que se puede abrir lo decide el
// nucleo, y el dato canonico es su .info de libretro
// (libretro-super/dist/info/*.info, campo supported_extensions). Cada linea
// es la UNION de los nucleos plausibles de ese sistema: si cualquiera de
// ellos lo abre, no se acusa. Es deliberado callar de mas antes que marcar
// como roto un fichero que funciona.
//
// Cadena vacia = no consta, y entonces no se afirma nada.
inline std::string playableExtensionsFor(const std::string& slug) {
    struct Pair { const char* slug; const char* exts; };
    static constexpr Pair kPlayable[] = {
        // azahar + citra_canary. Es la unica divergencia real de la
        // biblioteca: el servidor sirve .cia --un paquete de INSTALACION, no
        // una ROM-- y ningun nucleo de 3DS lo abre.
        {"3ds", ".3ds .3dsx .z3dsx .elf .axf .cci .zcci .cxi .zcxi .app"},
        // melonds + desmume
        {"nds", ".nds .ids .dsi .bin"},
        // mgba + vba_next
        {"gba", ".gba .gb .gbc"},
        // snes9x
        {"super-nintendo-entertainment-system",
         ".smc .sfc .swc .fig .bs .st"},
        {"snes", ".smc .sfc .swc .fig .bs .st"},
        // ppsspp
        {"psp", ".elf .iso .cso .prx .pbp .chd"},
        // dolphin, que cubre Wii y GameCube con la misma lista
        {"wii", ".gcm .iso .wbfs .ciso .gcz .elf .dol .dff .tgc .wad .rvz "
                ".m3u .wia"},
        {"gamecube", ".gcm .iso .wbfs .ciso .gcz .elf .dol .dff .tgc .wad "
                     ".rvz .m3u .wia"},
        {"gc", ".gcm .iso .wbfs .ciso .gcz .elf .dol .dff .tgc .wad .rvz "
               ".m3u .wia"},
        // Sistemas que tico admite y hoy no estan en el servidor. Si aparecen
        // manana la insignia ya sabe que decir.
        // fceumm + nestopia + mesen
        {"nes", ".nes .fds .unf .unif"},
        // gambatte + sameboy + mgba
        {"gb", ".gb .gbc .dmg .gba"},
        {"gbc", ".gb .gbc .dmg .gba"},
        // mupen64plus_next
        {"n64", ".n64 .v64 .z64 .ndd .bin .u1"},
        // swanstation + pcsx_rearmed + mednafen_psx_hw
        {"psx", ".exe .psexe .cue .bin .img .iso .chd .pbp .ecm .mds .psf "
                ".m3u .mdf .toc .cbn .ccd"},
        // genesis_plus_gx + picodrive
        {"genesis", ".mdx .md .smd .gen .bin .cue .iso .32x .68k .sgd .chd "
                    ".m3u .sc .pco"},
        {"megadrive", ".mdx .md .smd .gen .bin .cue .iso .32x .68k .sgd "
                      ".chd .m3u .sc .pco"},
        {"master-system", ".sms .bms .sg .bin .cue .chd .m3u"},
        {"game-gear", ".gg .sg .bin .cue .chd .m3u"},
        {"sega-cd", ".cue .iso .chd .m3u .ccd .bin"},
        // flycast
        {"dreamcast", ".chd .cdi .elf .bin .cue .gdi .lst .dat .m3u"},
        {"dc", ".chd .cdi .elf .bin .cue .gdi .lst .dat .m3u"},
        // Ausentes a proposito: psvita y psvita-ports. Vita3k no es un nucleo
        // libretro y no hay .info que consultar; ademas psvita-ports son .zip
        // de homebrew que ni siquiera van a la carpeta de tico.
    };
    for (const Pair& p : kPlayable)
        if (slug == p.slug)
            return p.exts;
    return {};
}

// Lo que se le puede decir al usuario sobre un fichero concreto.
enum class RomCompat {
    Unknown,     // no consta: no se afirma nada
    Playable,    // el nucleo lo abre
    Unplayable,  // el nucleo no lo abre, y no habria dado ningun error
};

// Un comprimido no se juzga: los frontends libretro suelen extraerlo al vuelo
// y un .zip es como se distribuye media biblioteca de 8 y 16 bits. Marcarlo
// como incompatible seria acusar en falso justo el caso mas comun.
inline bool isArchiveExtension(const std::string& ext) {
    return ext == ".zip" || ext == ".7z" || ext == ".rar";
}

// Extension en minusculas, con el punto. Vacia si no tiene.
inline std::string extensionOf(const std::string& fileName) {
    const size_t dot = fileName.find_last_of('.');
    if (dot == std::string::npos)
        return {};
    std::string ext = fileName.substr(dot);
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

// Veredicto de una extension suelta (con el punto). Lo usa la lista de
// plataformas, que sabe que formatos sirve el servidor mucho antes de ver un
// solo nombre de fichero.
inline RomCompat compatOfExtension(const std::string& slug,
                                   const std::string& rawExt) {
    const std::string playable = playableExtensionsFor(slug);
    if (playable.empty())
        return RomCompat::Unknown;
    std::string ext = rawExt;
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext.empty() || ext[0] != '.' || isArchiveExtension(ext))
        return RomCompat::Unknown;
    // Se busca la extension delimitada, para que ".cci" no case dentro de
    // ".zcci" ni ".3ds" dentro de ".3dsx".
    const std::string needle = ext + " ";
    return (playable + " ").find(needle) == std::string::npos
               ? RomCompat::Unplayable
               : RomCompat::Playable;
}

inline RomCompat romCompat(const std::string& slug,
                           const std::string& fileName) {
    return compatOfExtension(slug, extensionOf(fileName));
}

// Cierto solo cuando consta que el emulador NO lo abre. Sin dato no se acusa.
inline bool isUnplayableByEmulator(const std::string& slug,
                                   const std::string& fileName) {
    return romCompat(slug, fileName) == RomCompat::Unplayable;
}

} // namespace roms
} // namespace gamehubnx
