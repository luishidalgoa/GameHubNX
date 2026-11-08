// Prueba de la funcion pura de titulos de tarjeta (src/ui/common/card_title.hpp):
// dos lineas como mucho, 18 caracteres por linea, "…" cuando sobra texto.
#include <cassert>
#include <cstdio>
#include <string>

#include "ui/common/card_title.hpp"

using gamehubnx::ui::cardTitleText;
using gamehubnx::ui::utf8Length;

static void check(const std::string& got, const std::string& want, const char* what) {
    if (got != want) {
        std::fprintf(stderr, "FAIL %s\n  got:  [%s]\n  want: [%s]\n", what, got.c_str(), want.c_str());
        assert(false);
    }
}

int main() {
    // Titulo corto: tal cual.
    check(cardTitleText("Pokémon Omega Ruby"), "Pokémon Omega Ruby", "corto, UTF-8 de 18 caracteres");
    check(cardTitleText(""), "", "vacio");
    check(cardTitleText("   "), "", "solo espacios");

    // Dos lineas que caben: sin puntos suspensivos.
    check(cardTitleText("Fire Emblem Fates: Special Edition"),
          "Fire Emblem Fates:\nSpecial Edition", "dos lineas exactas");

    // Sobra texto: la segunda linea acaba en "…" y ninguna supera 18 caracteres.
    check(cardTitleText("The Legend of Zelda: Ocarina of Time 3D Remastered Edition"),
          "The Legend of\nZelda: Ocarina of\xE2\x80\xA6", "tres lineas se cortan en dos");

    // Palabra mas larga que una linea: se parte en trozos de 18, sin ellipsis si todo cabe.
    check(cardTitleText("Supercalifragilisticexpialidocious"),
          "Supercalifragilist\nicexpialidocious", "palabra larga partida");

    // Palabra muy larga: dos trozos y ellipsis al final.
    const std::string verylong = cardTitleText("Abcdefghijklmnopqrstuvwxyzabcdefghijklmnop");
    check(verylong, "Abcdefghijklmnopqr\nstuvwxyzabcdefghi…", "palabra muy larga");

    // Ningun linea supera 18 caracteres en ningun caso.
    const std::string many = cardTitleText(
        "Un titulo muy muy largo con demasiadas palabras para que quepa en dos lineas de tarjeta");
    std::size_t start = 0;
    int lines = 0;
    while (start <= many.size()) {
        const std::size_t end = many.find('\n', start);
        const std::string line = many.substr(start, end == std::string::npos ? std::string::npos : end - start);
        assert(utf8Length(line) <= 18);
        ++lines;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    assert(lines == 2);
    assert(many.size() >= 3 && many.compare(many.size() - 3, 3, "\xE2\x80\xA6") == 0);

    std::printf("test_card_title: ok\n");
    return 0;
}
