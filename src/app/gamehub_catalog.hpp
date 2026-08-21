#pragma once

#include <string>
#include <vector>

#include "catalog_service.hpp"

namespace pipensx {

// The built-in GameHub section.
//
// GameHub is a self-hosted personal library that serves its Switch shelf over
// plain HTTPS in the Tinfoil/CyberFoil index format. Unlike the torrent
// catalogue there is no swarm, no magnet and no info dictionary: every entry is
// a single URL that can be fetched directly, so entries produced here carry
// `directUrl` and are routed around the torrent engine.
//
// The endpoint is fixed rather than configurable. This fork exists to serve one
// specific library; a host setting would only be one more thing to get wrong.
namespace gamehub {

// Root index. Sub-indexes for DLC and updates are announced by the payload
// itself in `directories` and are followed from there, never hardcoded.
extern const char* const kCatalogUrl;

// Section label shown in the UI and used as the catalogue's source label.
extern const char* const kSourceLabel;

/// How this app identifies itself to GameHub, as `name/version`.
///
/// Sent in the `X-GameHub-Client` header. GameHub records it against every
/// download, which is how its traffic panel can tell an actual Switch pulling a
/// file apart from a browser or a Tinfoil-family client — and, because the
/// version rides along, which consoles are still on an old build.
extern const char* const kClientId;

/// Un Title ID de Switch dice que es la entrada por sus ultimos digitos:
/// `...000` juego base, `...800` actualizacion, cualquier otro DLC. Vacio o
/// mal formado se trata como base, para no esconder una entrada por no saber
/// clasificarla.
bool isBaseTitleId(const std::string& titleId);

/// El Title ID del juego base al que pertenece esta entrada.
///
/// En Switch los complementos derivan del id del juego: la actualizacion es
/// `...800` y los DLC van de `...1000` en adelante. Los trece bits bajos son el
/// discriminante, asi que ponerlos a cero devuelve el id base tanto si se parte
/// del juego como de una actualizacion o un DLC.
///
/// Cadena vacia si el id no es un Title ID de 16 digitos hexadecimales.
std::string baseTitleIdOf(const std::string& titleId);


// Parses a Tinfoil-style shop index into catalogue entries.
//
// Shape consumed (everything else in the payload is ignored):
//   files[]     – { url, size }: the downloadable titles
//   titledb{}   – optional rich metadata keyed by Nintendo Title ID
//   directories – sub-index URLs, returned in `subIndexes` for the caller to
//                 fetch and parse in turn
//
// Entries get a synthetic `infoHash`: the SHA-1 of their URL. The rest of the
// app keys downloads, dedup and persisted queue state off that field, so a
// stable identity is required even though no torrent exists. Deriving it from
// the URL keeps it stable across refreshes without inventing state.
//
// Returns false and fills `error` when the payload is not a usable index. A
// malformed individual entry is skipped, not fatal: a single bad row should
// never cost the user the rest of their library.
bool parseCatalog(const std::string& json, std::vector<CatalogEntry>& out,
                  std::vector<std::string>& subIndexes, std::string& error);

// Last path segment of a URL, percent-decoded: the file name as the user sees
// it. Shared with the provider, which needs it for the transfer's display name.
std::string fileNameFromUrl(const std::string& url);

// True when `url` may serve bytes for the GameHub section: same scheme, host
// and port as the configured endpoint. The catalogue is trusted precisely
// because it is the user's own server, so anything off-host is not it.
bool isTrustedUrl(const std::string& url);

} // namespace gamehub

} // namespace pipensx
