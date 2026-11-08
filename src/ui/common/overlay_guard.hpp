#pragma once

#include <cstddef>
#include <vector>

// Decide si hay algo encima de la pantalla principal (un dialogo, una ficha, un
// desplegable): en ese caso la reja no se reconstruye, porque liberaria celdas que
// borealis todavia tiene en su pila de foco. La pantalla base del arranque (la de la
// carga) se queda debajo de la principal durante toda la sesion y no cuenta: si
// contara, la pila siempre tendria dos actividades y la reja nunca se reconstruiria
// al cambiar de pestana. Funcion pura (sin interfaz), para poder probarla en el PC.
namespace gamehubnx::ui {

// La pantalla base del arranque, si la hay. Se fija al crearla y no se borra.
template <typename Activity>
Activity*& startupBaseSlot() {
    static Activity* slot = nullptr;
    return slot;
}

// `stack` va de la base a la cima. Hay overlay si, sin contar la base del arranque,
// quedan mas de una actividad: la principal mas algo encima.
template <typename Activity>
bool stackHasOverlay(const std::vector<Activity*>& stack, const Activity* startupBase) {
    std::size_t above = 0;
    for (const Activity* activity : stack)
        if (activity != startupBase)
            ++above;
    return above > 1;
}

}  // namespace gamehubnx::ui
