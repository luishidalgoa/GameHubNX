// Reduccion de iconos en memoria: la media por bloques y el tope de tamaño.

#include "app/rgba_image.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

namespace {

using gamehubnx::shrinkRgbaToFit;

// Bitmap de w x h con el canal rojo a `red(x, y)` y el resto a cero.
template <typename Red>
std::vector<uint8_t> makeRed(int w, int h, Red red) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* p = px.data() + (static_cast<size_t>(y) * w + x) * 4;
            p[0] = red(x, y);
            p[3] = 255;
        }
    return px;
}

void testAveragesBlocks() {
    // 4x2 con factor 2: dos bloques de 2x2. Izquierdo rojo 10, derecho 50.
    std::vector<uint8_t> px = makeRed(4, 2, [](int x, int) {
        return static_cast<uint8_t>(x < 2 ? 10 : 50);
    });
    int w = 4;
    int h = 2;
    shrinkRgbaToFit(px, w, h, 2);
    assert(w == 2 && h == 1);
    assert(px.size() == 2 * 1 * 4);
    assert(px[0] == 10);
    assert(px[4] == 50);
    assert(px[3] == 255);
}

void testAveragesRoundDown() {
    // Bloque 2x2 con rojos 1, 2, 3, 4: media 2 (suma 10 entre 4).
    std::vector<uint8_t> px = makeRed(2, 2, [](int x, int y) {
        return static_cast<uint8_t>(1 + x + 2 * y);
    });
    int w = 2;
    int h = 2;
    shrinkRgbaToFit(px, w, h, 1);
    assert(w == 1 && h == 1);
    assert(px[0] == 2);
}

void testIconSizeHalvesTo128() {
    // Un icono de NRO es 256x256; la casilla lo pinta a 120 px.
    std::vector<uint8_t> px = makeRed(256, 256, [](int, int) { return uint8_t(77); });
    int w = 256;
    int h = 256;
    shrinkRgbaToFit(px, w, h, 128);
    assert(w == 128 && h == 128);
    assert(px.size() == 128u * 128u * 4u);
    assert(px[0] == 77);
}

void testFittingImageIsUntouched() {
    std::vector<uint8_t> px = makeRed(3, 3, [](int, int) { return uint8_t(9); });
    const std::vector<uint8_t> before = px;
    int w = 3;
    int h = 3;
    shrinkRgbaToFit(px, w, h, 3);
    assert(w == 3 && h == 3);
    assert(px == before);
}

void testCannotShrinkToEmptyIsUntouched() {
    // 5x1 con tope 2: el factor 3 dejaria 0 filas, asi que no se toca.
    std::vector<uint8_t> px = makeRed(5, 1, [](int, int) { return uint8_t(1); });
    const std::vector<uint8_t> before = px;
    int w = 5;
    int h = 1;
    shrinkRgbaToFit(px, w, h, 2);
    assert(w == 5 && h == 1);
    assert(px == before);
}

} // namespace

int main() {
    testAveragesBlocks();
    testAveragesRoundDown();
    testIconSizeHalvesTo128();
    testFittingImageIsUntouched();
    testCannotShrinkToEmptyIsUntouched();
    std::printf("test_rgba_image: ok\n");
    return 0;
}
