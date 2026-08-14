#pragma once

#include <functional>
#include <string>

#include "debrid_provider.hpp"

namespace pipensx {

// GameHub as a DebridProvider.
//
// The debrid contract is "hand me a handle, I hand you back a direct download
// URL". GameHub is the degenerate case of that: the URL is already known from
// the catalogue, so there is nothing to create, poll or select — the transfer
// is Ready the moment it starts. Implementing the interface instead of writing
// a second downloader means the whole existing pipeline (ranged fetch, retry,
// backoff, streaming install, progress reporting) is reused untouched.
//
// The handle IS the URL. It is opaque to everything above, and deriving it from
// nothing keeps the provider stateless: no transfer table, nothing to clean up,
// nothing to leak if the app dies mid-download.
class GameHubProvider : public DebridProvider {
public:
    // Probes a URL for its size. Returns false when the size cannot be
    // established; the caller then reports 0, which costs a progress bar but
    // never blocks the download. Injected so tests need no network — the same
    // shape the transfer layer already uses for RangeFetcher.
    using SizeProbe = std::function<bool(const std::string& url,
                                         uint64_t& bytes,
                                         std::string& error)>;

    // Default probe: an HTTPS HEAD, pinned to the catalogue's scheme.
    static SizeProbe curlSizeProbe();

    GameHubProvider();
    explicit GameHubProvider(SizeProbe probe);

    bool validate(std::string& error) override;
    // The "magnet" is the direct URL carried by the catalogue entry. Rejected
    // unless it points at the configured GameHub host: this provider must never
    // be talked into fetching from somewhere else.
    bool createFromMagnet(const std::string& magnet, std::string& id,
                          std::string& error) override;
    // No torrent file exists for a GameHub entry; there is nothing to read.
    bool createFromFile(const std::string& torrentPath, std::string& id,
                        std::string& error) override;
    bool fetchInfo(const std::string& id, DebridInfo& info,
                   std::string& error) override;
    // One URL is one file: there is no selection to make.
    bool selectFiles(const std::string& id,
                     const std::vector<std::string>& fileIds,
                     std::string& error) override;
    bool resolveDownloadUrl(const std::string& id, const DebridInfo& info,
                            size_t kthSelected, const DebridFile& file,
                            std::string& url, std::string& error) override;
    // Nothing was created server-side, so nothing needs removing.
    bool remove(const std::string& id, std::string& error) override;
    const char* name() const override;

private:
    SizeProbe probe_;
};

} // namespace pipensx
