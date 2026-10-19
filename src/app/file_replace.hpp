#pragma once

#include <cstdio>
#include <string>

namespace gamehubnx {

// Pone `from` en el sitio de `to`, reemplazando lo que hubiera. El rename() de la
// Switch (fsdev/FatFs) falla si el destino ya existe, asi que el viejo se aparta
// primero (to.old) y se borra solo cuando el nuevo ya esta en su sitio. Si el
// segundo rename falla, el viejo vuelve a su nombre: nunca se pierde el fichero
// que habia.
inline bool replaceFile(const std::string& from, const std::string& to) {
    if (std::rename(from.c_str(), to.c_str()) == 0)
        return true;
    const std::string old = to + ".old";
    std::remove(old.c_str());
    if (std::rename(to.c_str(), old.c_str()) != 0)
        return false;  // no hay destino que apartar: el fallo es otro
    if (std::rename(from.c_str(), to.c_str()) == 0) {
        std::remove(old.c_str());
        return true;
    }
    std::rename(old.c_str(), to.c_str());
    return false;
}

} // namespace gamehubnx
