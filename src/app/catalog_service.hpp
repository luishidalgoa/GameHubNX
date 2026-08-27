#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pipensx {

enum class CatalogHealth {
    Unknown,
    Ok,
    NoPeers,
    MetadataTimeout,
    TrackerNotRegistered,
    Replaced,
    Dead,
};

struct CatalogEntry {
    std::string infoHash;
    std::string title;
    std::string magnetUri;
    std::string trackerUrl;
    std::string posterUrl;
    std::vector<std::string> screenshots;
    std::string healthReason;
    /* Inline catalogue metadata carried by the Langegen switch_games.json
       source. An optional metadata index is keyed by info-hash and may not
       match these entries, so the detail card falls back to these fields for
       its facts table, description and cover. Empty when absent. */
    std::string year;
    std::string genre;
    std::string developer;
    std::string publisher;
    std::string description;
    /* Langegen fields added after the first switch_games dump: Nintendo
       title id when known, CFW/firmware note, and a free-text multiplayer
       summary (Russian scrape text — not structured player modes).
       interface_lang / voice_lang are Russian releaser notes; the detail
       card shows them only in the Russian locale. */
    std::string titleId;
    std::string performance;
    std::string multiplayer;
    std::string interfaceLang;
    std::string voiceLang;
    /* Pre-resolved bencoded info dictionary (RF_ACCESS_PLAN П2.1), decoded
       from the catalog's base64 "info_dict" and SHA-1-verified against the
       magnet hash at parse time. Empty when the catalog carries none. */
    std::vector<uint8_t> infoDict;
    /* Direct HTTP(S) URL for entries that are not torrents at all — the
       GameHub section serves a self-hosted library over plain HTTP. Non-empty
       here means "fetch this URL", and the torrent engine is bypassed.
       Empty for every torrent-backed entry, which is the default. */
    /* Metadatos que solo trae la tienda GameHub (claves gh_ de su titledb).
       region/languages son datos estructurados de la biblioteca del usuario, a
       diferencia de interfaceLang/voiceLang, que son notas libres del releaser
       ruso y la ficha solo muestra en su locale. groupKey une las ediciones de
       un mismo juego para que no aparezcan como fichas sueltas. */
    std::string region;
    std::string languages;
    std::string trailerUrl;
    std::string groupKey;
    std::string directUrl;
    uint64_t topicId = 0;
    uint64_t size = 0;
    int64_t publishedAt = 0;
    int64_t sourceUpdatedAt = 0;
    int64_t catalogGeneratedAt = 0;
    int64_t lastCheckedAt = 0;
    uint32_t forumId = 0;
    uint32_t trackerId = 0;
    uint32_t peerCount = 0;
    CatalogHealth health = CatalogHealth::Unknown;
    bool metadataOk = false;

    /* Cierto cuando la entrada viene de la tienda GameHub: es una descarga
       HTTPS directa contra el servidor del usuario y no toca el motor de
       torrent ni ningun servicio de terceros. */
    bool isDirectDownload() const { return !directUrl.empty(); }

    /* El identificador que se le entrega al proveedor de transferencia. Para
       un torrent es el magnet; para la tienda es la propia URL, que es lo que
       GameHubProvider usa como handle. Tenerlo aqui evita que cada sitio que
       inicia una descarga tenga que acordarse de cual de los dos campos mirar
       -- olvidarlo es justo lo que dejaba la tienda sin instalar. */
    const std::string& transferHandle() const {
        return directUrl.empty() ? magnetUri : directUrl;
    }

    bool isHiddenByDefault() const {
        return health == CatalogHealth::Dead ||
               health == CatalogHealth::Replaced ||
               health == CatalogHealth::TrackerNotRegistered;
    }
};

// Built-in Langegen switch_games.json URL used when catalogSourceUrl is empty.
std::string defaultCatalogSourceUrl();

class CatalogService {
public:
    explicit CatalogService(std::string rootPath,
                            std::string bundledPath = {});

    bool load(std::string& error);

    /* Proporcion de portada (ancho/alto) que publica la tienda, o 0 si el
       indice no la trae. La UI la usa para dar a la caja la forma que el
       usuario configuro en GameHub en vez de una constante compilada. */
    float coverAspect() const {
        return (coverWidth_ > 0 && coverHeight_ > 0)
            ? static_cast<float>(coverWidth_) / static_cast<float>(coverHeight_)
            : 0.0f;
    }

    /* true si el sello de la cache coincide con la fuente efectiva actual.
       Una cache sin sello (version anterior, u otra app) se rechaza. */
    bool cacheMatchesSource() const;

    // Pool-thread safe: fetch the latest catalogue from the trusted source,
    // parse it, and persist the on-disk cache. Fills `parsed` on success and
    // never touches entries_, so it may run on a worker thread. The caller
    // adopts the parsed batch on the UI thread via adopt().
    bool fetchLatest(std::vector<CatalogEntry>& parsed, std::string& error,
                     const std::string& sourceUrl);
    // UI-thread only: adopt a freshly fetched batch as the live catalogue.
    // entries() is read unsynchronised by the render thread every frame, so
    // entries_ may only be reassigned here — never from a fetch worker.
    void adopt(std::vector<CatalogEntry> parsed,
               const std::string& sourceUrl = {});

    const std::vector<CatalogEntry>& entries() const { return *entries_; }

    // Linear scan for a catalog entry by info-hash (case-insensitive). Null
    // when absent. Used by the game-update flow: the metadata index is
    // catalog∩titledb, so the entry carries the magnet/info_dict for the hash
    // the index alone cannot resolve.
    const CatalogEntry* findByInfoHash(const std::string& infoHash) const;

    // Immutable shared snapshot of the live catalogue. Observers on other
    // threads (the web companion) hold this instead of a ~10 MB deep copy;
    // adopt() publishes a fresh vector, so a held snapshot stays valid and
    // consistent for as long as the pointer is kept.
    std::shared_ptr<const std::vector<CatalogEntry>> sharedEntries() const {
        return entries_;
    }

    // Runs on the UI thread at the end of every adopt(), so observers that
    // keep their own reference to the catalogue (the web companion) stay
    // current no matter which refresh path adopted the batch.
    void setOnAdopt(
        std::function<void(std::shared_ptr<const std::vector<CatalogEntry>>)>
            cb) {
        onAdopt_ = std::move(cb);
    }
    const std::string& sourceLabel() const { return sourceLabel_; }
    const std::string& rootPath() const { return rootPath_; }

    // Wall-clock seconds for when the live snapshot was written or loaded
    // (cache/bundled mtime, or refresh time after adopt). 0 when empty.
    int64_t snapshotEpochSec() const { return snapshotEpochSec_; }

    static bool parseJson(const std::string& json,
                          std::vector<CatalogEntry>& entries,
                          std::string& error);

    // True when `url` is allowed to serve catalog bytes for `sourceUrl`.
    // Built-in Langegen keeps the historical repo-prefix allowlist; a custom
    // source trusts only redirects within the source file's directory.
    static bool isTrustedSource(const std::string& url,
                                const std::string& sourceUrl);

private:
    /* Ruta de la cache de un sub-indice (DLC, actualizaciones). Se guardan
       aparte del raiz y con el mismo formato, asi que se reparsean con el
       mismo parseJson y no hace falta inventar un formato combinado. */
    std::string subIndexCachePath(size_t n) const;

    /* Anade a entries_ lo que haya en las caches de sub-indice. Silencioso si
       no hay ninguna: una biblioteca sin DLC es un caso normal. */
    void appendCachedSubIndexes();

    bool loadFile(const std::string& path, const std::string& label,
                  std::string& error);

    std::string rootPath_;
    int coverWidth_ = 0;
    int coverHeight_ = 0;
    std::string catalogRoot_;
    std::string cachePath_;
    /* Fichero hermano de la cache con la URL que la genero. Sin el, una cache
       escrita por otra fuente se cargaba como propia. */
    std::string sourceStampPath_;
    std::string bundledPath_;
    // Never null — starts as an empty vector. Reassigned only on the UI
    // thread (see adopt()).
    std::shared_ptr<const std::vector<CatalogEntry>> entries_ =
        std::make_shared<const std::vector<CatalogEntry>>();
    std::string sourceLabel_;
    int64_t snapshotEpochSec_ = 0;
    std::function<void(std::shared_ptr<const std::vector<CatalogEntry>>)>
        onAdopt_;
};

} // namespace pipensx
