// Resumen del contenido de un torrent a partir de los nombres de sus archivos.
#include "../src/app/torrent_contents.hpp"

#include <cassert>
#include <cstdio>

using namespace gamehubnx;

int main() {
    std::vector<ContentFile> files = {
        {"Life is Strange Double Exposure [0100B2301F4A8000][v0][US].nsz", 12000},
        {"Life is Strange Double Exposure [0100B2301F4A8800][v65536][US].nsz", 400},
        {"Life is Strange DLC [0100B2301F4A9001][v0].nsp", 100},
        {"Life is Strange DLC part 1 [0100B2301F4A9001][v0].nsp", 50},
        {"Life is Strange DLC [0100B2301F4A9002][v0].nsp", 70},
        {"readme.txt", 5},
    };
    const ContentsSummary s = summarizeContents(files);

    assert(s.games == 1);
    assert(s.gameBytes == 12000);
    assert(s.updates == 1);
    assert(s.updateBytes == 400);
    // Dos DLC distintos; el de dos ficheros cuenta una vez.
    assert(s.dlcs == 2);
    assert(s.dlcBytes == 220);
    assert(s.unclassified == 1);
    assert(s.unclassifiedBytes == 5);
    assert(s.totalBytes() == 12000 + 400 + 220 + 5);
    assert(s.baseTitleId == "0100B2301F4A8000");
    assert(s.dlcTitleIds.count("0100B2301F4A9001") == 1);

    // Sin ningún Title ID: todo sin clasificar, sin juego base.
    const ContentsSummary none = summarizeContents({{"a.nsz", 1}, {"b.nsp", 2}});
    assert(none.games == 0 && none.unclassified == 2 && none.baseTitleId.empty());

    std::printf("torrent contents tests passed\n");
    return 0;
}
