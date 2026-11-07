// Comprueba que un DLC o una actualización no se instala sin su juego base.
#include "../src/app/base_title_guard.hpp"
// Ambas cabeceras en la misma unidad: si comparten nombre, no compila.
#include "../src/app/download_kind.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using gamehubnx::baseTitleIdFor;
using gamehubnx::missingBaseFor;
using gamehubnx::packageTitleId;

int main() {
    // El Title ID sale del nombre, en mayúsculas.
    assert(packageTitleId("Minecraft v1.21.92 [0100d71004694800][v8257536].nsp") == "0100D71004694800");
    assert(packageTitleId("Sin id.nsp").empty());
    // La otra función, con su propio tipo de retorno, sigue siendo la suya.
    assert(gamehubnx::titleIdInName("Juego [0100BA4026E94800].nsp") == 0x0100BA4026E94800ull);
    assert(packageTitleId("[0100D7100469480].nsp").empty());   // 15 hex: no vale

    // Juego base, actualización y DLC: el base de cada uno.
    assert(baseTitleIdFor("0100D71004694000") == "0100D71004694000");
    assert(baseTitleIdFor("0100D71004694800") == "0100D71004694000");
    assert(baseTitleIdFor("01006BD001E07036") == "01006BD001E06000");   // DLC del Switch Edition
    assert(baseTitleIdFor("01006BD001E06800") == "01006BD001E06000");   // su actualización
    assert(baseTitleIdFor("corto").empty());

    const auto none = [](const std::string&) { return false; };
    const auto all = [](const std::string&) { return true; };

    // El base se puede instalar siempre.
    assert(missingBaseFor("Minecraft (2009) [0100D71004694000][v0].nsp", none).empty());
    // Un DLC sin base instalado no se instala, y se dice cuál falta.
    assert(missingBaseFor("Minecraft Nintendo Switch Edition [DLC Skin Pack 1] [01006BD001E07036][v0].nsp", none)
           == "01006BD001E06000");
    // Con el base instalado, sí.
    assert(missingBaseFor("Minecraft Nintendo Switch Edition [DLC Skin Pack 1] [01006BD001E07036][v0].nsp", all).empty());
    // Un nombre sin Title ID no se bloquea: no hay forma de saber de qué juego es.
    assert(missingBaseFor("Juego sin id.nsp", none).empty());

    std::printf("base title guard tests passed\n");
    return 0;
}
