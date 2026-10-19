#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gamehubnx {

// Lee `length` bytes desde `offset` de un .nro: un fichero local o un rango HTTP.
// Devuelve false si no se pudo leer exactamente lo pedido.
using NroReader = std::function<bool(uint64_t offset, size_t length,
                                     std::vector<uint8_t>& out)>;

// Donde estan las secciones de un .nro con cabecera de assets (ASET). Los
// desplazamientos de icono y NACP son relativos al inicio de ASET.
struct NroAssetLayout {
    uint64_t assetOffset = 0;
    uint64_t iconOffset = 0;
    uint64_t iconSize = 0;
    uint64_t nacpOffset = 0;
    uint64_t nacpSize = 0;
};

// Lo que el NACP dice de la app. Es lo que muestra la consola en el menu.
struct NroInfo {
    std::string name;
    std::string author;
    std::string displayVersion;
};

// Cabecera NRO0 + cabecera ASET. Falla si no es un .nro o no trae assets.
bool readNroAssetLayout(const NroReader& read, NroAssetLayout& out,
                        std::string& error);

// Nombre, autor y version visible del NACP.
bool readNroInfo(const NroReader& read, NroInfo& out, std::string& error);

// Icono del .nro: JPEG tal cual, listo para decodificar.
bool readNroIcon(const NroReader& read, std::vector<uint8_t>& jpeg,
                 std::string& error);

// Lector sobre un fichero local (la SD o un .nro de prueba).
NroReader localNroReader(const std::string& path);

} // namespace gamehubnx
