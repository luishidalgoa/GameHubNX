#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace gamehubnx {

// Tipo de contenido de una descarga. Lo dice el Title ID que lleva su nombre
// entre corchetes: el juego base, su actualizacion o un DLC. La misma regla que
// isUpdateTitleId / isDlcTitleId de gamehub_catalog, para no depender del catalogo.
enum class DownloadKind { Game, Update, Dlc };

// Title ID de 16 digitos hex entre corchetes ("[0100BA4026E94800]"). 0 si el
// nombre no lo lleva.
inline uint64_t titleIdInName(const std::string& name) {
    for (size_t i = 0; i + 18 <= name.size(); ++i) {
        if (name[i] != '[' || name[i + 17] != ']')
            continue;
        uint64_t value = 0;
        bool valid = true;
        for (size_t j = 1; j <= 16 && valid; ++j) {
            const char c = name[i + j];
            if (c >= '0' && c <= '9')
                value = (value << 4) | static_cast<uint64_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value = (value << 4) | static_cast<uint64_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value = (value << 4) | static_cast<uint64_t>(c - 'A' + 10);
            else
                valid = false;
        }
        if (valid)
            return value;
    }
    return 0;
}

inline DownloadKind downloadKindOf(const std::string& name) {
    const uint64_t disc = titleIdInName(name) & 0x1FFFu;
    if (disc == 0x800u)
        return DownloadKind::Update;
    if (disc >= 0x1000u)
        return DownloadKind::Dlc;
    return DownloadKind::Game;
}

} // namespace gamehubnx
