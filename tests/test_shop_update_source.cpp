// La tienda como unica fuente de versiones de actualizacion.
//
// Una actualizacion cuenta solo si la sirve la tienda: entrada del sub-indice
// con enlace directo y `[vN]` en el nombre. Lo que anuncia el indice de
// metadatos no entra aqui: ese era el origen de la actualizacion que no existia.

#include "app/shop_update_source.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using gamehubnx::CatalogEntry;
using gamehubnx::ShopUpdateSource;

constexpr const char* Game = "0100ABC000000000";    // juego base
constexpr const char* Update = "0100ABC000000800";  // su actualizacion
constexpr const char* Dlc = "0100ABC000001000";     // un DLC
constexpr const char* OtherGame = "0100XYZ000000800";

// Entrada de la tienda tal como la deja el sub-indice /updates.
CatalogEntry shopEntry(const std::string& titleId, const std::string& file) {
    CatalogEntry entry;
    entry.titleId = titleId;
    entry.directUrl = "https://gamehub.hdglabs.com/api/shop/updates/" + file;
    entry.fromSubIndex = true;
    return entry;
}

// Entrada que NO es de la tienda: solo existe en el indice de metadatos.
CatalogEntry indexOnlyEntry(const std::string& titleId) {
    CatalogEntry entry;
    entry.titleId = titleId;
    return entry;
}

void testCollectsOnlyShopUpdatesOfTheGame() {
    const std::vector<CatalogEntry> entries = {
        shopEntry(Update, "Game%20%5Bv65536%5D.nsp"),
        shopEntry(Update, "Game%20%5Bv131072%5D.nsp"),
        shopEntry(Dlc, "Game%20DLC%20%5Bv0%5D.nsp"),          // DLC, no parche
        shopEntry(OtherGame, "Other%20%5Bv999999%5D.nsp"),    // otro juego
        indexOnlyEntry(Update),                                // sin enlace
    };
    std::vector<std::string> versions;
    assert(gamehubnx::collectShopUpdateVersions(entries, Game, versions));
    assert(versions.size() == 2);
    assert(versions[0] == "65536");
    assert(versions[1] == "131072");

    // Da igual si la consola reporta el id base o el de la actualizacion.
    std::vector<std::string> fromUpdateId;
    assert(gamehubnx::collectShopUpdateVersions(entries, Update, fromUpdateId));
    assert(fromUpdateId.size() == 2);

    assert(gamehubnx::shopLatestVersion(entries, Game) == "131072");
}

void testNoShopEntryMeansNoSource() {
    // Un juego que solo aparece en el indice de metadatos no tiene fuente: el
    // check debe decir "sin fuente", no inventar una version nueva.
    const std::vector<CatalogEntry> entries = {indexOnlyEntry(Update)};
    std::vector<std::string> versions;
    assert(!gamehubnx::collectShopUpdateVersions(entries, Game, versions));
    assert(versions.empty());
    assert(gamehubnx::shopLatestVersion(entries, Game).empty());
}

void testUntaggedShopUpdateIsNotOffered() {
    // Sin [vN] no hay forma de saber que version es, y no se ofrece.
    const std::vector<CatalogEntry> entries = {
        shopEntry(Update, "Game%20Update.nsp"),
    };
    std::vector<std::string> versions;
    assert(!gamehubnx::collectShopUpdateVersions(entries, Game, versions));
    assert(versions.empty());
}

void testSourceWithoutCatalogHasNoVersions() {
    ShopUpdateSource source(nullptr);
    std::vector<std::string> versions;
    assert(!source.collectLatestVersions(Game, versions));
    assert(versions.empty());
}

} // namespace

int main() {
    testCollectsOnlyShopUpdatesOfTheGame();
    testNoShopEntryMeansNoSource();
    testUntaggedShopUpdateIsNotOffered();
    testSourceWithoutCatalogHasNoVersions();
    std::printf("test_shop_update_source: ok\n");
    return 0;
}
