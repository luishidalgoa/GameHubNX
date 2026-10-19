#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gamehubnx {

// De donde puede salir el icono de una app. Se prueban en este orden: la
// cache del disco, el .nro instalado, el .nro del release (un rango HTTP) y el
// icon.png del paquete de HB App Store.
struct HomebrewIconSource {
    std::string cacheKey;   // nombre del fichero en la cache, sin extension
    std::string localNro;   // .nro instalado en la SD, si lo hay
    std::string remoteNro;  // URL del .nro del release (GitHub)
    std::string iconUrl;    // icon.png del paquete (HB App Store)
};

// sdmc:/switch/gamehubnx/icons
std::string homebrewIconCacheDir();

// Bytes del icono (JPEG o PNG) tal cual llegan. Guarda en la cache lo que consiga.
bool loadHomebrewIconBytes(const HomebrewIconSource& source,
                           std::vector<uint8_t>& bytes, std::string& error);

// Decodifica a RGBA (4 bytes por pixel).
bool decodeHomebrewIcon(const std::vector<uint8_t>& bytes,
                        std::vector<uint8_t>& rgba, int& width, int& height);

} // namespace gamehubnx
