#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "app/catalog_service.hpp"
#include "app/game_update_install.hpp"
#include "app/gamehub_catalog.hpp"
#include "app/update_source.hpp"

namespace gamehubnx {

// La TIENDA es la unica fuente de "hay una version nueva".
//
// Una actualizacion cuenta solo si el servidor la sirve: una entrada del
// sub-indice /updates, con enlace directo y `[vN]` en el nombre del fichero. El
// indice de metadatos ya no decide nada. Su `latestVersion` sale de titledb y
// puede anunciar una version que no existe en la biblioteca; al instalar, la
// app caia a la mas alta disponible --mas vieja-- y volvia a sugerir la que
// faltaba. Ahora lo que se sugiere es, por construccion, lo que se puede
// instalar: la misma entrada que se descarga.
//
// Sin entradas de la tienda para el juego no hay fuente: el estado es
// "sin fuente", que es verdad, y no un "al dia" inventado.
inline bool collectShopUpdateVersions(const std::vector<CatalogEntry>& entries,
                                      const std::string& titleId,
                                      std::vector<std::string>& out) {
    const std::string baseId = gamehub::baseTitleIdOf(titleId);
    if (baseId.empty())
        return false;
    bool any = false;
    for (const CatalogEntry& entry : entries) {
        if (entry.directUrl.empty() || !entry.fromSubIndex)
            continue;                       // no es de la tienda, o es un DLC
        if (!gamehub::isUpdateTitleId(entry.titleId))
            continue;
        if (gamehub::baseTitleIdOf(entry.titleId) != baseId)
            continue;
        uint64_t tag = 0;
        // Sin etiqueta no hay forma de saber que version es: no se ofrece.
        if (!fileVersionTag(gamehub::fileNameFromUrl(entry.directUrl), tag))
            continue;
        out.push_back(std::to_string(tag));
        any = true;
    }
    return any;
}

// La version mas alta que sirve la tienda para este juego, en decimal. Vacia
// si la tienda no tiene ninguna. Es lo que muestra la ficha del juego.
inline std::string shopLatestVersion(const std::vector<CatalogEntry>& entries,
                                     const std::string& titleId) {
    std::vector<std::string> versions;
    if (!collectShopUpdateVersions(entries, titleId, versions))
        return {};
    std::string best;
    uint64_t bestValue = 0;
    for (const std::string& version : versions) {
        const uint64_t value = std::strtoull(version.c_str(), nullptr, 10);
        if (best.empty() || value > bestValue) {
            best = version;
            bestValue = value;
        }
    }
    return best;
}

// Fuente de GameUpdateService: solo el catalogo de la tienda.
class ShopUpdateSource : public IUpdateMetadataSource {
public:
    explicit ShopUpdateSource(const CatalogService* catalog)
        : catalog_(catalog) {}

    bool collectLatestVersions(const std::string& titleId,
                               std::vector<std::string>& out) const override {
        return catalog_ &&
               collectShopUpdateVersions(catalog_->entries(), titleId, out);
    }

private:
    const CatalogService* catalog_;
};

} // namespace gamehubnx
