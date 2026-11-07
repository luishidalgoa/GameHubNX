#include "torrent_contents_service.hpp"

#include "download_manager.hpp"

#include <cstdio>

#include <sys/stat.h>
#include <unistd.h>

namespace gamehubnx {

namespace {

bool isFile(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

} // namespace

bool loadTorrentContents(const std::string& torrentsRoot,
                         const std::string& infoHash,
                         const std::string& magnet,
                         std::atomic<bool>& cancelled,
                         const MagnetResolver::ProgressCallback& progress,
                         ContentsSummary& out,
                         std::string& error) {
    const std::string path = torrentsRoot + "/" + infoHash + ".torrent";
    if (!isFile(path)) {
        const std::string partial = path + ".part";
        MagnetResolver resolver;
        if (!resolver.resolveToFile(magnet, partial, cancelled, progress, error)) {
            ::unlink(partial.c_str());
            return false;
        }
        if (std::rename(partial.c_str(), path.c_str()) != 0) {
            ::unlink(partial.c_str());
            error = "Unable to keep the torrent metadata.";
            return false;
        }
    }

    TorrentPreview preview;
    if (!DownloadManager::previewTorrent(path, preview, error)) {
        // Un .torrent que no se lee no sirve para nada: se tira para resolverlo de nuevo.
        ::unlink(path.c_str());
        return false;
    }
    std::vector<ContentFile> files;
    files.reserve(preview.files.size());
    for (const TorrentPreview::File& file : preview.files)
        files.push_back({file.path, file.length});
    out = summarizeContents(files);
    return true;
}

} // namespace gamehubnx
