// Prueba de la decision de overlay de la reja (src/ui/common/overlay_guard.hpp).
// Regresion: con la pantalla base del arranque debajo de la principal, la pila tenia
// siempre dos actividades y la reja nunca se reconstruia al cambiar de pestana.
#include <cassert>
#include <cstdio>
#include <vector>

#include "ui/common/overlay_guard.hpp"

namespace {
struct Fake {};
}

using gamehubnx::ui::stackHasOverlay;

int main() {
    Fake base, loading, main, dialog;

    // Sin nada: no hay overlay.
    assert(!stackHasOverlay<Fake>({}, &base));

    // Arranque: base y pantalla de carga, o base y principal. Nada encima.
    assert(!stackHasOverlay<Fake>({&base, &loading}, &base));
    assert(!stackHasOverlay<Fake>({&base, &main}, &base));

    // Uso normal con la base debajo: la principal sola, sin overlay.
    assert(!stackHasOverlay<Fake>({&base, &main}, &base));

    // Algo encima de la principal: overlay, con o sin base.
    assert(stackHasOverlay<Fake>({&base, &main, &dialog}, &base));
    assert(stackHasOverlay<Fake>({&main, &dialog}, nullptr));

    // Sin base: la principal sola no es overlay.
    assert(!stackHasOverlay<Fake>({&main}, nullptr));

    std::printf("test_overlay_guard: ok\n");
    return 0;
}
