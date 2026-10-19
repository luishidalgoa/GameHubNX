// Clasificacion de descargas por tipo, a partir del Title ID del nombre.

#include "app/download_kind.hpp"

#include <cassert>
#include <cstdio>

using gamehubnx::DownloadKind;
using gamehubnx::downloadKindOf;
using gamehubnx::titleIdInName;

int main() {
    // Juego base (…0000), actualizacion (…0800) y DLC (…1000 y superiores).
    assert(downloadKindOf("Pikmin 1 (2023) [0100AA80194B0000][v0].nsz") == DownloadKind::Game);
    assert(downloadKindOf("Castlevania Belmonts Curse [0100BA4026E94800][v65536].nsp") ==
           DownloadKind::Update);
    assert(downloadKindOf("Castlevania Belmonts Curse [DLC Bonus Contents Pack] [0100BA4026E95001][v0].nsp") ==
           DownloadKind::Dlc);
    // Minusculas tambien valen.
    assert(downloadKindOf("juego [0100aa80194b0800][v1].nsz") == DownloadKind::Update);
    // Sin Title ID: se trata como juego, para no esconderlo.
    assert(downloadKindOf("sin-title-id.nsp") == DownloadKind::Game);
    assert(titleIdInName("sin-title-id.nsp") == 0);
    // Corchetes con algo que no es un Title ID no cuentan.
    assert(titleIdInName("[v65536] [0100BA4026E94800]") == 0x0100BA4026E94800ull);
    assert(titleIdInName("[0100BA4026E9480G]") == 0);
    std::printf("test_download_kind: ok\n");
    return 0;
}
