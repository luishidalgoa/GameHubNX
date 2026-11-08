#pragma once

#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "download_manager.hpp"
#include "game_update_install.hpp"
#include "gamehub_catalog.hpp"
#include "magnet_resolver.hpp"
#include "torrent_update_source.hpp"

namespace gamehubnx {

// Sondea el torrent de un juego: resuelve su magnet (metadatos por peers), lee
// los nombres de sus ficheros y guarda las versiones de actualizacion que trae.
// Bloqueante: se llama desde un hilo de fondo, nunca desde la interfaz. Si el
// magnet no responde se devuelve false y la version guardada no cambia.
inline bool probeTorrentUpdates(TorrentUpdateSource& source,
                                const std::string& titleId,
                                const std::string& magnet,
                                const std::string& tmpDir,
                                std::string& error) {
    const std::string tmp = tmpDir + "/_update_probe.torrent";
    MagnetResolver resolver;
    std::atomic<bool> cancelled {false};
    std::vector<uint8_t> peers;
    if (!resolver.resolveToFile(magnet, tmp, cancelled, {}, error, &peers,
                                nullptr)) {
        ::unlink(tmp.c_str());
        return false;
    }
    TorrentPreview preview;
    const bool previewed = DownloadManager::previewTorrent(tmp, preview, error);
    ::unlink(tmp.c_str());
    if (!previewed)
        return false;
    std::vector<std::string> paths;
    paths.reserve(preview.files.size());
    for (const auto& file : preview.files)
        paths.push_back(file.path);
    source.setVersions(titleId, torrentUpdateVersions(paths, titleId));
    return true;
}

} // namespace gamehubnx
