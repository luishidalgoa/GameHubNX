#pragma once

// Qué contiene un torrent antes de bajarlo: juego base, actualizaciones y DLC.
//
// La lista de archivos sale de la metadata (el diccionario info), que no incluye el
// contenido. Cada archivo lleva su Title ID entre corchetes, así que el tipo sale del
// nombre: los trece bits bajos a cero son el juego base, 0x800 la actualización y
// cualquier otro valor un DLC. Pura: no toca red ni disco.

#include "base_title_guard.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace gamehubnx {

struct ContentFile {
    std::string path;
    uint64_t length = 0;
};

struct ContentsSummary {
    /// Títulos distintos (no archivos): un DLC de varios ficheros cuenta una vez.
    uint32_t games = 0;
    uint32_t updates = 0;
    uint32_t dlcs = 0;
    /// Archivos sin Title ID reconocible: se listan pero no se clasifican.
    uint32_t unclassified = 0;
    uint64_t gameBytes = 0;
    uint64_t updateBytes = 0;
    uint64_t dlcBytes = 0;
    uint64_t unclassifiedBytes = 0;
    /// Juego base al que pertenecen las actualizaciones y los DLC ("" si no se sabe).
    std::string baseTitleId;
    std::set<std::string> updateTitleIds;
    std::set<std::string> dlcTitleIds;

    uint64_t totalBytes() const {
        return gameBytes + updateBytes + dlcBytes + unclassifiedBytes;
    }
};

inline ContentsSummary summarizeContents(const std::vector<ContentFile>& files) {
    ContentsSummary out;
    std::set<std::string> gameTitles;
    for (const ContentFile& file : files) {
        const std::string titleId = packageTitleId(file.path);
        if (titleId.size() != 16) {
            ++out.unclassified;
            out.unclassifiedBytes += file.length;
            continue;
        }
        const std::string base = baseTitleIdFor(titleId);
        if (out.baseTitleId.empty())
            out.baseTitleId = base;
        if (base == titleId) {
            gameTitles.insert(titleId);
            out.gameBytes += file.length;
        } else if (titleId.substr(13, 3) == "800") {
            out.updateTitleIds.insert(titleId);
            out.updateBytes += file.length;
        } else {
            out.dlcTitleIds.insert(titleId);
            out.dlcBytes += file.length;
        }
    }
    out.games = static_cast<uint32_t>(gameTitles.size());
    out.updates = static_cast<uint32_t>(out.updateTitleIds.size());
    out.dlcs = static_cast<uint32_t>(out.dlcTitleIds.size());
    return out;
}

} // namespace gamehubnx
