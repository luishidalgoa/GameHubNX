#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace gamehubnx {

// Un DLC o una actualización no se puede instalar sin su juego base: el contenido
// se engancha a la base y, sin ella, el juego queda roto o con una versión distinta.
//
// El Title ID se lee del nombre del paquete ("Juego [0100D71004694800][v8257536].nsp").
// Los trece bits bajos dicen qué es: cero, el juego base; 0x800, la actualización;
// cualquier otro valor, un DLC, que cuelga del base 0x1000 por encima del suyo.

// El Title ID de 16 hex entre corchetes, en mayúsculas, o "" si el nombre no lo tiene.
inline std::string titleIdInName(const std::string& name) {
    for (size_t i = 0; i + 18 <= name.size(); ++i) {
        if (name[i] != '[' || name[i + 17] != ']')
            continue;
        bool hex = true;
        for (size_t j = i + 1; j < i + 17 && hex; ++j) {
            const char c = name[j];
            hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }
        if (!hex)
            continue;
        std::string id = name.substr(i + 1, 16);
        for (char& c : id)
            if (c >= 'a' && c <= 'f')
                c = static_cast<char>(c - 'a' + 'A');
        return id;
    }
    return {};
}

// El Title ID del juego base del que depende el id dado (el propio id si ya es el base).
inline std::string baseTitleIdFor(const std::string& titleId) {
    if (titleId.size() != 16)
        return {};
    uint64_t id = 0;
    for (char c : titleId) {
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else return {};
        id = id * 16 + static_cast<uint64_t>(digit);
    }
    const uint64_t low = id & 0xFFFull;
    uint64_t base = id;
    if (low == 0x800ull)
        base = id & ~0x800ull;
    else if (low != 0)
        base = (id & ~0xFFFull) - 0x1000ull;
    char out[17];
    std::snprintf(out, sizeof(out), "%016llX", static_cast<unsigned long long>(base));
    return out;
}

// El juego base que falta para instalar este paquete, o "" si puede instalarse.
// `baseIsPresent` dice si el base ya está en la consola o va a estar (en cola).
inline std::string missingBaseFor(const std::string& name,
                                  const std::function<bool(const std::string&)>& baseIsPresent) {
    const std::string id = titleIdInName(name);
    if (id.empty())
        return {};
    const std::string base = baseTitleIdFor(id);
    if (base.empty() || base == id)
        return {};  // es el juego base: nada que comprobar
    return baseIsPresent(base) ? std::string{} : base;
}

} // namespace gamehubnx
