// Parser for the built-in GameHub section: a self-hosted library served in the
// Tinfoil/CyberFoil index format. Fixtures mirror the real payload shape —
// URL-encoded file names, a titledb keyed by Title ID, announced sub-indexes.

#include "app/gamehub_catalog.hpp"
#include "app/rom_paths.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include <sys/stat.h>

using namespace gamehubnx;

namespace {

// Same origin as kCatalogUrl, so trust checks pass. Anything else must not.
const char* const kHost = "https://gamehub.hdglabs.com";

std::string sample() {
    return std::string(R"({
      "files": [
        { "url": ")" + std::string(kHost) + R"(/api/shop/download/250/Animal%20Crossing%20New%20Horizons%20(2020)%20%5B01006F8002326000%5D%5BUS%5D%5Bv0%5D.nsp",
          "size": 6694761920 },
        { "url": ")" + std::string(kHost) + R"(/api/shop/download/311/Untitled%20Game.nsp",
          "size": 12345 }
      ],
      "directories": [
        ")" + std::string(kHost) + R"(/api/shop/dlc",
        ")" + std::string(kHost) + R"(/api/shop/updates",
        "https://evil.example.com/api/shop/dlc"
      ],
      "titledb": {
        "01006F8002326000": {
          "id": "01006F8002326000",
          "name": "Animal Crossing: New Horizons",
          "description": "Island life.",
          "releaseDate": 20200101,
          "category": ["Simulation"],
          "publisher": "Nintendo",
          "size": 6694761920,
          "iconUrl": ")" + std::string(kHost) + R"(/api/covers/proxy/covers/switch/250.webp",
          "bannerUrl": ")" + std::string(kHost) + R"(/api/covers/proxy/covers/switch/250.webp"
        }
      },
      "success": "GameHub · 2 titles"
    })");
}

void test_parses_files() {
    std::vector<CatalogEntry> out;
    std::vector<std::string> subs;
    std::string err;
    assert(gamehub::parseCatalog(sample(), out, subs, err));
    assert(err.empty());
    assert(out.size() == 2);
}

void test_titledb_metadata_applied() {
    std::vector<CatalogEntry> out;
    std::vector<std::string> subs;
    std::string err;
    assert(gamehub::parseCatalog(sample(), out, subs, err));

    const CatalogEntry& e = out[0];
    assert(e.titleId == "01006F8002326000");
    assert(e.title == "Animal Crossing: New Horizons");   // titledb wins over the file name
    assert(e.publisher == "Nintendo");
    assert(e.year == "2020");                             // YYYYMMDD -> year only
    assert(e.genre == "Simulation");                      // first of the category list
    assert(e.description == "Island life.");
    assert(e.size == 6694761920ULL);
    assert(e.metadataOk);
    assert(e.health == CatalogHealth::Ok);
}

void test_falls_back_to_file_name() {
    std::vector<CatalogEntry> out;
    std::vector<std::string> subs;
    std::string err;
    assert(gamehub::parseCatalog(sample(), out, subs, err));

    // No titledb row for this one: the decoded file name has to carry it.
    const CatalogEntry& e = out[1];
    assert(e.title == "Untitled Game.nsp");   // %20 decoded back to spaces
    assert(e.titleId.empty());
    assert(!e.metadataOk);
    assert(e.size == 12345);
}

void test_direct_url_and_synthetic_hash() {
    std::vector<CatalogEntry> out;
    std::vector<std::string> subs;
    std::string err;
    assert(gamehub::parseCatalog(sample(), out, subs, err));

    for (const CatalogEntry& e : out) {
        assert(!e.directUrl.empty());        // routed around the torrent engine
        assert(e.magnetUri.empty());         // and carrying no torrent identity
        assert(e.infoHash.size() == 40);     // still keyed like everything else
        for (char c : e.infoHash)
            assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    assert(out[0].infoHash != out[1].infoHash);
}

void test_hash_is_stable_across_parses() {
    std::vector<CatalogEntry> a, b;
    std::vector<std::string> sa, sb;
    std::string ea, eb;
    assert(gamehub::parseCatalog(sample(), a, sa, ea));
    assert(gamehub::parseCatalog(sample(), b, sb, eb));
    // Queue state persists across refreshes and is keyed by this: it must not
    // wander between runs.
    assert(a[0].infoHash == b[0].infoHash);
}

void test_subindexes_only_from_our_host() {
    std::vector<CatalogEntry> out;
    std::vector<std::string> subs;
    std::string err;
    assert(gamehub::parseCatalog(sample(), out, subs, err));

    assert(subs.size() == 2);                       // the evil.example.com one is dropped
    for (const std::string& s : subs)
        assert(gamehub::isTrustedUrl(s));
}

void test_untrusted_cover_rejected() {
    const std::string json = std::string(R"({
      "files": [ { "url": ")" + std::string(kHost) + R"(/api/shop/download/1/A%20%5B0100000000000000%5D.nsp", "size": 5 } ],
      "titledb": { "0100000000000000": { "name": "A", "iconUrl": "https://evil.example.com/x.png" } }
    })");
    std::vector<CatalogEntry> out;
    std::vector<std::string> subs;
    std::string err;
    assert(gamehub::parseCatalog(json, out, subs, err));
    assert(out.size() == 1);
    assert(out[0].title == "A");
    assert(out[0].posterUrl.empty());   // off-host cover refused
}

void test_bad_rows_are_skipped_not_fatal() {
    const std::string json = std::string(R"({
      "files": [
        { "size": 5 },
        { "url": "" },
        "not-an-object",
        { "url": ")" + std::string(kHost) + R"(/api/shop/download/9/Good.nsp", "size": 7 }
      ]
    })");
    std::vector<CatalogEntry> out;
    std::vector<std::string> subs;
    std::string err;
    // One malformed row must never cost the user the rest of the library.
    assert(gamehub::parseCatalog(json, out, subs, err));
    assert(out.size() == 1);
    assert(out[0].title == "Good.nsp");
}

void test_rejects_unusable_payloads() {
    std::vector<CatalogEntry> out;
    std::vector<std::string> subs;
    std::string err;

    assert(!gamehub::parseCatalog("not json at all", out, subs, err));
    assert(!err.empty());

    err.clear();
    assert(!gamehub::parseCatalog("[1,2,3]", out, subs, err));   // array, not object
    assert(!err.empty());

    err.clear();
    assert(!gamehub::parseCatalog(R"({"success":"hi"})", out, subs, err));  // no files
    assert(!err.empty());
}

void test_trust_check() {
    assert(gamehub::isTrustedUrl(std::string(kHost) + "/api/shop/dlc"));
    assert(gamehub::isTrustedUrl(std::string(kHost) + "/anything"));
    assert(!gamehub::isTrustedUrl("https://evil.example.com/api/shop"));
    assert(!gamehub::isTrustedUrl("https://gamehub.hdglabs.com.evil.com/x"));
    assert(!gamehub::isTrustedUrl("not-a-url"));
    assert(!gamehub::isTrustedUrl(""));
}



// Un indice con la extension gh_: es lo que sirve nuestra tienda.
const char* const kRichIndex = R"JSON({
  "files": [ { "url": "https://gamehub.hdglabs.com/api/shop/download/7/Zelda%20%5B0100000000010000%5D.nsp", "size": 1024 } ],
  "titledb": {
    "0100000000010000": {
      "id": "0100000000010000",
      "name": "The Legend of Zelda (EUR)",
      "description": "Una aventura.",
      "publisher": "Nintendo",
      "gh_title": "The Legend of Zelda",
      "gh_region": "EUR",
      "gh_languages": "es,en,fr",
      "gh_developer": "Nintendo EPD",
      "gh_year": 2017,
      "gh_group": "zelda-botw",
      "gh_trailer": "https://www.youtube.com/watch?v=abc123",
      "gh_screenshots": [
        "https://gamehub.hdglabs.com/shots/1.jpg",
        "https://evil.example.com/2.jpg"
      ]
    }
  },
  "success": "ok"
})JSON";

void test_rich_metadata_is_read() {
    std::vector<CatalogEntry> out;
    std::vector<std::string> subs;
    std::string err;
    assert(gamehub::parseCatalog(kRichIndex, out, subs, err));
    assert(out.size() == 1);
    const CatalogEntry& e = out[0];

    // gh_title gana a `name`: este viene con la region pegada porque Tinfoil
    // solo tiene ese campo, y la ficha quiere maquetarlos por separado.
    assert(e.title == "The Legend of Zelda");
    assert(e.region == "EUR");
    assert(e.languages == "es,en,fr");
    assert(e.developer == "Nintendo EPD");
    assert(e.publisher == "Nintendo");
    assert(e.year == "2017");
    assert(e.groupKey == "zelda-botw");
    assert(e.trailerUrl == "https://www.youtube.com/watch?v=abc123");

    // Solo la captura del propio servidor sobrevive: la otra se descarga y se
    // pinta, asi que un host ajeno no entra.
    assert(e.screenshots.size() == 1);
    assert(e.screenshots[0].find("gamehub.hdglabs.com") != std::string::npos);
}

void test_index_without_gh_keys_still_works() {
    // El indice de otra tienda, sin la extension: menos datos, nunca un fallo.
    const char* plain = R"JSON({
      "files": [ { "url": "https://gamehub.hdglabs.com/api/shop/download/1/Game%20%5B0100000000020000%5D.nsp", "size": 10 } ],
      "titledb": { "0100000000020000": { "id": "0100000000020000", "name": "Game" } },
      "success": "ok"
    })JSON";
    std::vector<CatalogEntry> out;
    std::vector<std::string> subs;
    std::string err;
    assert(gamehub::parseCatalog(plain, out, subs, err));
    assert(out.size() == 1);
    assert(out[0].title == "Game");
    assert(out[0].region.empty());
    assert(out[0].screenshots.empty());
    assert(out[0].trailerUrl.empty());
}



void test_clasificacion_de_title_id() {
    // Juego base: los trece bits bajos a cero.
    assert(gamehub::isBaseTitleId("0100000000010000"));
    // Actualizacion: ...800.
    assert(!gamehub::isBaseTitleId("0100000000010800"));
    // DLC: de ...1000 en adelante.
    assert(!gamehub::isBaseTitleId("0100000000011000"));
    assert(!gamehub::isBaseTitleId("010000000001100f"));
    // Sin id, o con uno que no es de Switch, se trata como base: mas vale
    // ensenar una entrada de mas que esconderla por no saber clasificarla.
    assert(gamehub::isBaseTitleId(""));
    assert(gamehub::isBaseTitleId("no-es-un-title-id"));
}

void test_complemento_apunta_a_su_juego() {
    const std::string base = "0100000000010000";
    // Los tres derivan al mismo juego.
    assert(gamehub::baseTitleIdOf(base) == base);
    assert(gamehub::baseTitleIdOf("0100000000010800") == base);   // update
    assert(gamehub::baseTitleIdOf("0100000000011000") == base);   // DLC 1
    assert(gamehub::baseTitleIdOf("0100000000011007") == base);   // DLC 8
    // Mayusculas: se normaliza a minusculas, como el resto del catalogo.
    assert(gamehub::baseTitleIdOf("0100000000011007") ==
           gamehub::baseTitleIdOf("0100000000011007"));
    assert(gamehub::baseTitleIdOf("01005F8014A78000") == "01005f8014a78000");
    // Un juego DISTINTO no debe colarse como complemento del anterior.
    assert(gamehub::baseTitleIdOf("0100000000020000") != base);
    // Entradas invalidas devuelven vacio, nunca un id inventado.
    assert(gamehub::baseTitleIdOf("").empty());
    assert(gamehub::baseTitleIdOf("0100").empty());
    assert(gamehub::baseTitleIdOf("zzzzzzzzzzzzzzzz").empty());
}

// ── Plataformas ──────────────────────────────────────────────────────────────

void test_lista_de_plataformas() {
    const std::string json = R"({"files":[
      {"slug":"switch","name":"Nintendo Switch","extensions":[".nsp",".nsz"],
       "games":72,"native":true,"emulator":null},
      {"slug":"gba","name":"GBA","extensions":[".gba"],
       "games":100,"native":false,"emulator":"Eden"}
    ],"success":"ok"})";
    std::vector<gamehub::ShopPlatform> out;
    std::string err;
    assert(gamehub::parsePlatforms(json, out, err));
    assert(out.size() == 2);
    assert(out[0].slug == "switch" && out[0].native);
    assert(out[0].games == 72);
    assert(out[1].slug == "gba" && !out[1].native);
    assert(out[1].extensions.size() == 1 && out[1].extensions[0] == ".gba");
    assert(out[1].emulator == "Eden");
}

void test_icono_de_plataforma_solo_del_servidor() {
    const std::string json = R"({"files":[
      {"slug":"gba","gh_icon":"https://gamehub.hdglabs.com/api/covers/proxy/icons/gba.webp?fmt=jpg"},
      {"slug":"psp","gh_icon":"https://evil.example.com/icons/psp.jpg"}
    ]})";
    std::vector<gamehub::ShopPlatform> out;
    std::string err;
    assert(gamehub::parsePlatforms(json, out, err));
    assert(out.size() == 2);
    assert(!out[0].iconUrl.empty());
    // Un indice manipulado no va a hacer que la consola pida imagenes a
    // cualquier sitio: la plataforma sigue siendo utilizable, sin icono.
    assert(out[1].iconUrl.empty());
}

void test_plataforma_sin_nombre_usa_el_slug() {
    // Peor rotulo, pero la plataforma sigue siendo utilizable: lo unico que
    // hace falta para pedirle el indice es el slug.
    const std::string json = R"({"files":[{"slug":"psp"}]})";
    std::vector<gamehub::ShopPlatform> out;
    std::string err;
    assert(gamehub::parsePlatforms(json, out, err));
    assert(out.size() == 1 && out[0].name == "psp");
}

void test_plataforma_sin_slug_se_salta() {
    // Sin slug no hay nada que pedir despues, asi que la fila no vale para
    // nada. Se salta, y las demas siguen.
    const std::string json =
        R"({"files":[{"name":"Rota"},{"slug":""},{"slug":"wii","name":"Wii"}]})";
    std::vector<gamehub::ShopPlatform> out;
    std::string err;
    assert(gamehub::parsePlatforms(json, out, err));
    assert(out.size() == 1 && out[0].slug == "wii");
}

void test_indice_de_plataforma_marca_sus_entradas() {
    const std::string json = R"({"files":[
      {"url":"https://gamehub.example.com/api/shop/download/7/Metroid.gba",
       "size":5741824,"gh_id":7,"gh_kind":"rom","gh_title":"Metroid Fusion"}
    ]})";
    std::vector<CatalogEntry> out;
    std::string err;
    assert(gamehub::parsePlatformIndex(json, "gba", out, err));
    assert(out.size() == 1);
    // La marca es lo que distingue una ROM de un paquete instalable: sin ella
    // el resto de la app la trataria como un juego de Switch.
    assert(out[0].platformSlug == "gba");
    assert(out[0].isRom());
    assert(out[0].isDirectDownload());
}

void test_indice_de_plataforma_exige_slug() {
    const std::string json = R"({"files":[]})";
    std::vector<CatalogEntry> out;
    std::string err;
    assert(!gamehub::parsePlatformIndex(json, "", out, err));
    assert(!err.empty());
}

void test_una_entrada_de_switch_no_es_rom() {
    std::vector<CatalogEntry> out;
    std::vector<std::string> subs;
    std::string err;
    assert(gamehub::parseCatalog(sample(), out, subs, err));
    assert(!out.empty());
    for (const CatalogEntry& e : out)
        assert(!e.isRom());
}

// ── Rutas de ROM ─────────────────────────────────────────────────────────────

void test_rutas_de_tico() {
    // Nombres COMPROBADOS de tico. No es una preferencia estetica: tico no lee
    // una carpeta que no se llame exactamente asi (ticohq/tico#148), y una
    // equivocada significa una ROM descargada que no aparece en el frontend.
    assert(roms::defaultRomPath("gba") == "sdmc:/tico/roms/gba");
    assert(roms::defaultRomPath("psp") == "sdmc:/tico/roms/psp");
    assert(roms::defaultRomPath("wii") == "sdmc:/tico/roms/wii");
    // El slug largo del servidor no es el nombre que tico espera.
    assert(roms::defaultRomPath("super-nintendo-entertainment-system") ==
           "sdmc:/tico/roms/snes");
    assert(roms::defaultPathIsKnown("gba"));
    // Comprobado contra el arbol de una instalacion real: la carpeta de 3DS se
    // llama igual que el slug, y eso quita el aviso de 63 juegos.
    assert(roms::defaultRomPath("3ds") == "sdmc:/tico/roms/3ds");
    assert(roms::defaultPathIsKnown("3ds"));
}

void test_plataforma_desconocida_cae_al_slug() {
    // Sin correspondencia comprobada NO se adivina: se usa el slug, que al
    // menos es estable y unico, y la interfaz avisa de que hay que revisarla.
    // tico no crea carpeta para la DS --en el arbol comprobado no existe-- asi
    // que aqui no hay nada que confirmar y el aviso tiene que salir.
    assert(roms::defaultRomPath("nds") == "sdmc:/tico/roms/nds");
    assert(!roms::defaultPathIsKnown("nds"));
    assert(!roms::defaultPathIsKnown("psvita-ports"));
    // Y una plataforma que el servidor anada manana tampoco rompe nada.
    assert(roms::defaultRomPath("inventada") == "sdmc:/tico/roms/inventada");
    assert(!roms::defaultPathIsKnown("inventada"));
}

void test_ficha_de_un_juego() {
    const std::string json = R"({
      "gh_id": 271,
      "gh_platform": "3ds",
      "gh_platform_name": "Nintendo 3DS",
      "gh_title": "Resident Evil: Revelations",
      "gh_cover": "https://gamehub.hdglabs.com/api/covers/proxy/covers/3ds/271.webp?fmt=jpg",
      "gh_description": "Un survival horror a bordo de un crucero.",
      "gh_screenshots": [
        "https://gamehub.hdglabs.com/api/covers/proxy/shots/1.webp?fmt=jpg",
        "https://malo.example/robada.jpg"
      ],
      "gh_trailer": "https://www.youtube.com/watch?v=abc",
      "gh_developer": "Capcom",
      "gh_publisher": "Capcom",
      "gh_genre": "Survival horror",
      "gh_year": 2012,
      "gh_region": "E",
      "gh_languages": "es,en",
      "files": [
        {"url": "https://gamehub.hdglabs.com/api/shop/download/271/re.cia",
         "size": 3380741120, "gh_kind": "base", "gh_title": "Resident Evil"},
        {"url": "https://gamehub.hdglabs.com/api/shop/download/dlc/9/re-usa.cia",
         "size": 3380741121, "gh_kind": "region", "gh_region": "USA"},
        {"url": "https://otro.example/colado.cia", "size": 10,
         "gh_kind": "base"}
      ]
    })";
    gamehub::ShopGameDetail detail;
    std::string error;
    assert(gamehub::parseGameDetail(json, detail, error));
    assert(detail.id == 271);
    assert(detail.platformSlug == "3ds");
    assert(detail.title == "Resident Evil: Revelations");
    assert(detail.description.find("survival") != std::string::npos);
    assert(detail.developer == "Capcom");
    assert(detail.genre == "Survival horror");
    // El ano llega como numero y se guarda como texto, que es lo que pinta la
    // ficha.
    assert(detail.year == "2012");
    assert(detail.region == "E");
    assert(detail.languages == "es,en");

    // Imagenes SOLO de nuestro servidor: una ficha manipulada no va a hacer que
    // la consola pida imagenes a cualquier sitio.
    assert(detail.screenshots.size() == 1);
    assert(detail.screenshots[0].find("gamehub.hdglabs.com") !=
           std::string::npos);
    assert(!detail.coverUrl.empty());

    // El trailer SI puede ser de fuera: es un enlace de YouTube que se ensena
    // para verlo en otro sitio, la consola no lo abre ni lo descarga.
    assert(detail.trailerUrl == "https://www.youtube.com/watch?v=abc");

    // Y las descargas tambien se filtran por host: la tercera se cae.
    assert(detail.files.size() == 2);
    assert(detail.files[0].kind == "base");
    assert(detail.files[0].size == 3380741120ull);
    assert(detail.files[1].kind == "region");
    assert(detail.files[1].region == "USA");

    // Una ficha sin nada que descargar no sirve para lo que existe.
    gamehub::ShopGameDetail vacia;
    assert(!gamehub::parseGameDetail(
        R"({"gh_id":1,"gh_title":"x","files":[]})", vacia, error));
    assert(!error.empty());

    // Pero que falten campos sueltos se aguanta: una ficha a medias sigue
    // sirviendo para decidir, y una pantalla en blanco no.
    gamehub::ShopGameDetail minima;
    assert(gamehub::parseGameDetail(
        R"({"gh_id":5,"gh_title":"Solo lo justo","files":[
             {"url":"https://gamehub.hdglabs.com/api/shop/download/5/a.gba",
              "size":4194304}]})", minima, error));
    assert(minima.title == "Solo lo justo");
    assert(minima.description.empty());
    assert(minima.screenshots.empty());
    assert(minima.files.size() == 1);

    // Y una respuesta que no es JSON no revienta.
    gamehub::ShopGameDetail rota;
    assert(!gamehub::parseGameDetail("<html>404</html>", rota, error));
}

void test_carpeta_elegida_por_el_usuario() {
    std::map<std::string, std::string> chosen;
    // Sin nada elegido manda la tabla.
    assert(roms::romPathFor("gba", chosen) == "sdmc:/tico/roms/gba");

    // Y en cuanto se elige, manda lo elegido: quien no usa tico no tiene por
    // que aceptar su arbol.
    chosen["gba"] = "sdmc:/retroarch/roms/gba";
    assert(roms::romPathFor("gba", chosen) == "sdmc:/retroarch/roms/gba");
    // Sin tocar a las demas.
    assert(roms::romPathFor("psp", chosen) == "sdmc:/tico/roms/psp");

    // Una entrada vacia no cuenta como eleccion: dejaria las ROMs en la raiz.
    chosen["psp"] = "";
    assert(roms::romPathFor("psp", chosen) == "sdmc:/tico/roms/psp");

    // Leer una raiz que no existe da lista vacia, no un fallo: es la respuesta
    // util cuando tico no esta instalado.
    assert(roms::listRomFolders("/no/existe/esta/ruta").empty());

    // Y sobre una raiz de verdad devuelve solo carpetas, ordenadas.
    const std::string root = "/tmp/gamehubnx-romfolders-test";
    assert(system(("rm -rf " + root).c_str()) != -1);
    assert(mkdir(root.c_str(), 0755) == 0);
    assert(mkdir((root + "/snes").c_str(), 0755) == 0);
    assert(mkdir((root + "/gba").c_str(), 0755) == 0);
    FILE* stray = fopen((root + "/leeme.txt").c_str(), "w");
    assert(stray);
    fclose(stray);
    const std::vector<std::string> found = roms::listRomFolders(root);
    assert(found.size() == 2);
    assert(found[0] == "gba");
    assert(found[1] == "snes");
    assert(system(("rm -rf " + root).c_str()) != -1);
}

void test_nombre_de_rom_desde_el_titulo() {
    // Lo normal: el titulo se usa tal cual.
    assert(roms::romFileNameFromTitle("Chrono Trigger") == "Chrono Trigger");

    // Lo que la tarjeta no admite se cae, y el resto se conserva. Sin esto el
    // renombrado fallaria justo en los juegos con dos puntos en el titulo, que
    // son legion.
    assert(roms::romFileNameFromTitle("Zelda: A Link to the Past") ==
           "Zelda A Link to the Past");
    assert(roms::romFileNameFromTitle("Where? *What* /Who\\") ==
           "Where What Who");

    // Espacios repetidos se juntan, y ni espacio ni punto al final: el punto
    // lo pone la extension.
    assert(roms::romFileNameFromTitle("  Sonic   the   Hedgehog  ") ==
           "Sonic the Hedgehog");
    assert(roms::romFileNameFromTitle("Mario Kart.") == "Mario Kart");

    // Un titulo larguisimo se corta con hueco para la extension, y sin dejar
    // un espacio colgando en el corte.
    const std::string largo(300, 'a');
    assert(roms::romFileNameFromTitle(largo).size() == 120);

    // Y si no queda nada aprovechable se devuelve vacio, que es la senal de
    // "conserva el nombre original" en vez de dejar un fichero sin nombre.
    assert(roms::romFileNameFromTitle("///").empty());
    assert(roms::romFileNameFromTitle("   ").empty());
    assert(roms::romFileNameFromTitle("").empty());
}

void test_formatos_que_el_emulador_abre() {
    // La biblioteca de 3DS es casi toda .cia --un paquete de instalacion-- y el
    // emulador no lo abre. Se descarga bien y luego no aparece, sin ningun
    // error que lo explique, asi que hay que avisar antes.
    assert(roms::isUnplayableByEmulator("3ds", "Juego.cia"));
    assert(!roms::isUnplayableByEmulator("3ds", "Juego.3ds"));
    assert(!roms::isUnplayableByEmulator("3ds", "Juego.cci"));
    assert(!roms::isUnplayableByEmulator("3ds", "JUEGO.CXI"));   // sin distinguir mayusculas

    // Delimitado: ".cci" no puede casar DENTRO de ".zcci", ni ".3ds" dentro de
    // ".3dsx". Sin eso, un .zcci valido se marcaria como incompatible.
    assert(!roms::isUnplayableByEmulator("3ds", "Juego.zcci"));
    assert(!roms::isUnplayableByEmulator("3ds", "Juego.3dsx"));

    // El resto de la biblioteca SI encaja con lo que abre su nucleo, y eso
    // tambien hay que comprobarlo: un falso aviso en las mil filas de GBA
    // seria peor que no avisar.
    assert(!roms::isUnplayableByEmulator("gba", "Juego.gba"));
    assert(!roms::isUnplayableByEmulator("nds", "Juego.nds"));
    assert(!roms::isUnplayableByEmulator("psp", "Juego.chd"));
    assert(!roms::isUnplayableByEmulator("wii", "Juego.rvz"));
    assert(!roms::isUnplayableByEmulator(
        "super-nintendo-entertainment-system", "Juego.sfc"));

    // Sin lista para esa plataforma NO se acusa: callarse es mejor que marcar
    // como incompatible algo que quiza se abre perfectamente. Vita3k no es un
    // nucleo libretro y no hay .info que consultar.
    assert(!roms::isUnplayableByEmulator("psvita", "Juego.vpk"));
    assert(roms::playableExtensionsFor("psvita").empty());
    assert(roms::romCompat("psvita", "Juego.vpk") == roms::RomCompat::Unknown);

    // Un comprimido tampoco se juzga: los frontends libretro suelen extraerlo
    // al vuelo y asi se distribuye media biblioteca de 8 y 16 bits.
    assert(roms::romCompat("gba", "Juego.zip") == roms::RomCompat::Unknown);
    assert(roms::romCompat("nes", "Juego.7z") == roms::RomCompat::Unknown);

    // Un nombre sin extension tampoco se acusa.
    assert(!roms::isUnplayableByEmulator("3ds", "sin_extension"));
    assert(roms::romCompat("3ds", "sin_extension") ==
           roms::RomCompat::Unknown);

    // Lo que se pinta junta DOS preguntas, y la primera manda: de nada sirve
    // que el formato sea el correcto si tico no emula esa consola. La ROM se
    // descargaria, se dejaria en su sitio y no apareceria igual.
    assert(!roms::ticoSupportsPlatform("nds"));
    assert(roms::romCompat("nds", "Juego.nds") == roms::RomCompat::Playable);
    assert(roms::displayCompat("nds", "Juego.nds") ==
           roms::RomCompat::Unplayable);
    assert(roms::displayCompatForExtension("nds", ".nds") ==
           roms::RomCompat::Unplayable);
    // Y donde tico si llega, el veredicto sigue siendo el del formato.
    assert(roms::ticoSupportsPlatform("gba"));
    assert(roms::displayCompat("gba", "Juego.gba") ==
           roms::RomCompat::Playable);
    assert(roms::displayCompat("3ds", "Juego.cia") ==
           roms::RomCompat::Unplayable);

    // Los tres veredictos, que son los tres colores de la franja.
    assert(roms::romCompat("3ds", "Juego.3ds") == roms::RomCompat::Playable);
    assert(roms::romCompat("3ds", "Juego.cia") == roms::RomCompat::Unplayable);

    // La extension sale en minusculas y con el punto: es lo que se pinta en la
    // insignia y lo que se busca en la lista.
    assert(roms::extensionOf("JUEGO.CIA") == ".cia");
    assert(roms::extensionOf("un.nombre.raro.3ds") == ".3ds");
    assert(roms::extensionOf("sin_extension").empty());
}

} // namespace

int main() {
    test_parses_files();
    test_titledb_metadata_applied();
    test_falls_back_to_file_name();
    test_direct_url_and_synthetic_hash();
    test_hash_is_stable_across_parses();
    test_subindexes_only_from_our_host();
    test_untrusted_cover_rejected();
    test_bad_rows_are_skipped_not_fatal();
    test_rejects_unusable_payloads();
    test_trust_check();
    test_rich_metadata_is_read();
    test_index_without_gh_keys_still_works();
    test_clasificacion_de_title_id();
    test_complemento_apunta_a_su_juego();
    test_lista_de_plataformas();
    test_icono_de_plataforma_solo_del_servidor();
    test_plataforma_sin_nombre_usa_el_slug();
    test_plataforma_sin_slug_se_salta();
    test_indice_de_plataforma_marca_sus_entradas();
    test_indice_de_plataforma_exige_slug();
    test_una_entrada_de_switch_no_es_rom();
    test_rutas_de_tico();
    test_plataforma_desconocida_cae_al_slug();
    test_formatos_que_el_emulador_abre();
    test_carpeta_elegida_por_el_usuario();
    test_ficha_de_un_juego();
    test_nombre_de_rom_desde_el_titulo();
    std::printf("gamehub catalog tests passed\n");
    return 0;
}
