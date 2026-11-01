#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace gamehubnx {

/// Lo que un juego instalado tiene pendiente en la pestana de actualizaciones:
/// una actualizacion, DLC que la consola no tiene, o las dos cosas.
enum class RowAddOnKind { None, Update, Dlc, UpdateAndDlc };

/// Los DLC del catalogo que la consola aun no tiene, sin repetir. Es una
/// diferencia de conjuntos: un DLC instalado puede no estar en el catalogo, y
/// eso no cuenta como falta. El orden del catalogo se conserva.
inline std::vector<std::string> missingDlcTitleIds(
    const std::vector<std::string>& catalogDlc,
    const std::vector<std::string>& installedDlc) {
    const std::unordered_set<std::string> have(installedDlc.begin(),
                                               installedDlc.end());
    std::unordered_set<std::string> seen;
    std::vector<std::string> missing;
    for (const std::string& id : catalogDlc) {
        if (have.count(id) || !seen.insert(id).second)
            continue;
        missing.push_back(id);
    }
    return missing;
}

/// Que tiene pendiente una fila a partir de si hay actualizacion y si faltan DLC.
inline RowAddOnKind classifyRowAddOns(bool hasUpdate, bool hasMissingDlc) {
    if (hasUpdate && hasMissingDlc)
        return RowAddOnKind::UpdateAndDlc;
    if (hasUpdate)
        return RowAddOnKind::Update;
    if (hasMissingDlc)
        return RowAddOnKind::Dlc;
    return RowAddOnKind::None;
}

}  // namespace gamehubnx
