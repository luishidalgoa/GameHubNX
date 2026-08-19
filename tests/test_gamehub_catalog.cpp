// Parser for the built-in GameHub section: a self-hosted library served in the
// Tinfoil/CyberFoil index format. Fixtures mirror the real payload shape —
// URL-encoded file names, a titledb keyed by Title ID, announced sub-indexes.

#include "app/gamehub_catalog.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace pipensx;

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
    std::printf("gamehub catalog tests passed\n");
    return 0;
}
