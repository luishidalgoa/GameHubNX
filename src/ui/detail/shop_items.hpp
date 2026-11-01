#pragma once

// La lista de "Que instalar" de un juego y sus complementos. La usan la ficha
// del catalogo y la pestana de actualizaciones: las dos muestran el mismo
// desplegable, con los mismos elementos y las mismas marcas de instalado.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#include "app/catalog_service.hpp"
#include "app/gamehub_catalog.hpp"
#include "app/installed_title_service.hpp"
#include "ui/detail/shop_addons.hpp"

namespace gamehubnx::ui {

/// Complementos (actualizaciones y DLC) de un juego, sacados del catalogo
/// entero. `catalogKnows` dice si el catalogo cargado trae alguno: distinguir
/// "este juego no tiene DLC" de "aun no se han cargado los sub-indices".
struct AddOnSelection {
    std::vector<CatalogEntry> addOns;
    bool catalogKnows = false;
};

inline AddOnSelection addOnsOf(const std::vector<CatalogEntry>& entries,
                               const CatalogEntry& entry) {
    AddOnSelection out;
    const std::string base = gamehub::baseTitleIdOf(entry.titleId);
    for (const CatalogEntry& other : entries) {
        if (other.directUrl.empty())
            continue;                       // no es de la tienda
        if (!other.fromSubIndex && gamehub::isBaseTitleId(other.titleId))
            continue;                       // es un juego, no un extra
        out.catalogKnows = true;
        // El emparejamiento, por orden de fiabilidad:
        //
        //  1. gh_id. Es la relacion que el servidor conoce desde que escaneo
        //     los directorios. No depende del nombre del fichero.
        //  2. Title ID base. El respaldo, para catalogos que no son GameHub y
        //     para servidores que aun no publiquen gh_id.
        //
        // Sin ninguno de los dos no hay forma honesta de saber de que juego es
        // el complemento, y se deja fuera.
        if (entry.gameId != 0 && other.gameId != 0) {
            if (other.gameId != entry.gameId)
                continue;
        } else if (base.empty() ||
                   gamehub::baseTitleIdOf(other.titleId) != base) {
            continue;
        }
        // DLC y actualizacion, los dos: la pantalla de complementos ensena y
        // descarga ambos.
        out.addOns.push_back(other);
    }
    return out;
}

/// Version instalada de un juego base, en numero (0 cuando no se conoce).
inline uint64_t installedVersionOfTitle(const InstalledTitleService* installed,
                                        const std::string& titleId) {
    if (!installed || titleId.empty())
        return 0;
    auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        return text;
    };
    const std::string want = lower(titleId);
    for (const InstalledTitle& title : installed->titles()) {
        if (lower(title.titleId) != want || title.version.empty())
            continue;
        uint64_t value = 0;
        for (char c : title.version) {
            if (c < '0' || c > '9')
                return 0;
            value = value * 10 + static_cast<uint64_t>(c - '0');
        }
        return value;
    }
    return 0;
}

/// La lista de "Que instalar": el juego si no esta instalado, y sus
/// complementos. El juego solo aparece si NO esta instalado: ofrecer reinstalar
/// 6 GB junto a los DLC que faltan invita a marcarlo sin querer.
inline std::vector<ShopItem> buildShopItems(
    const InstalledTitleService* installed, const CatalogEntry& base,
    const std::vector<CatalogEntry>& addOns) {
    std::vector<ShopItem> items;
    const std::string baseId = gamehub::baseTitleIdOf(base.titleId);
    const bool baseInstalled =
        installed && !base.titleId.empty() && installed->contains(base.titleId);
    if (!baseInstalled) {
        ShopItem item;
        item.entry = base;
        item.kind = ShopItemKind::Game;
        items.push_back(std::move(item));
    }
    const uint64_t installedVersion = installedVersionOfTitle(installed, baseId);
    for (const CatalogEntry& entry : addOns) {
        ShopItem item;
        item.entry = entry;
        if (gamehub::isDlcTitleId(entry.titleId)) {
            item.kind = ShopItemKind::Dlc;
            item.installed = installed && !entry.titleId.empty() &&
                             installed->containsDlc(entry.titleId);
        } else {
            item.kind = ShopItemKind::Update;
            // Solo se marca cuando se pueden comparar las DOS versiones.
            // Decir "instalada" sin poder comprobarlo es peor que callarse.
            const uint64_t candidate = shopVersionFromName(entry.title);
            item.installed = installedVersion != 0 && candidate != 0 &&
                             installedVersion >= candidate;
        }
        items.push_back(std::move(item));
    }
    return items;
}

}  // namespace gamehubnx::ui
