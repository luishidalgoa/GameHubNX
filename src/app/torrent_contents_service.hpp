#pragma once

#include "torrent_contents.hpp"

#include <atomic>
#include <string>

#include "magnet_resolver.hpp"

namespace gamehubnx {

/// Contenido de un torrent sin descargarlo: su .torrent (solo metadata) y el resumen.
///
/// Si ya hay un .torrent guardado en `torrentsRoot`, no hace falta red. Si no, se pide la
/// metadata a los peers con el magnet, y el .torrent queda guardado para la siguiente vez
/// (es pequeño: unas decenas de KB, no contenido). Un fallo o una cancelación no deja
/// ficheros a medias.
bool loadTorrentContents(const std::string& torrentsRoot,
                         const std::string& infoHash,
                         const std::string& magnet,
                         std::atomic<bool>& cancelled,
                         const MagnetResolver::ProgressCallback& progress,
                         ContentsSummary& out,
                         std::string& error);

} // namespace gamehubnx
