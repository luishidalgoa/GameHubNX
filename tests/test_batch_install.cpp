#include "app/catalog_batch_installer.hpp"
#include "support/torbox_client.hpp"
#include "support/torbox_provider.hpp"

extern "C" {
#include "core/sha1.h"
}

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace gamehubnx;

static std::string makeTorrent(const std::string& directory) {
    const std::string payload = "test payload";
    uint8_t digest[20];
    sha1(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(),
         digest);

    std::string torrent = "d8:announce14:http://tracker4:infod6:lengthi";
    torrent += std::to_string(payload.size());
    torrent += "e4:name11:package.nsp12:piece lengthi";
    torrent += std::to_string(payload.size());
    torrent += "e6:pieces20:";
    torrent.append(reinterpret_cast<const char*>(digest), 20);
    torrent += "ee";

    std::string path = directory + "/source.torrent";
    std::ofstream output(path, std::ios::binary);
    output.write(torrent.data(), static_cast<std::streamsize>(torrent.size()));
    return path;
}

static bool copyFile(const std::string& source, const std::string& target) {
    std::ifstream input(source, std::ios::binary);
    std::ofstream output(target, std::ios::binary);
    output << input.rdbuf();
    return input.good() || input.eof();
}

int main() {
    char rootTemplate[] = "/tmp/gamehubnx-batch-XXXXXX";
    char* root = mkdtemp(rootTemplate);
    assert(root);

    // Source entry for the debrid blocks: they only need its title and link.
    CatalogEntry entry;
    entry.title = "Game";
    entry.infoHash = std::string(40, 'a');
    entry.magnetUri = "magnet:test";

    {
        // TorBox prepare: one entry that resolves to a single .nsp package.
        std::vector<std::pair<std::string, std::string>> script = {
            {"createtorrent", "{\"success\":true,\"data\":{\"torrent_id\":7}}"},
            {"mylist", "{\"success\":true,\"data\":{\"id\":7,\"name\":\"Game\","
                       "\"size\":1000,\"progress\":1.0,"
                       "\"download_state\":\"completed\","
                       "\"download_finished\":true,\"download_present\":true,"
                       "\"files\":[{\"id\":1,\"name\":\"game.nsp\","
                       "\"size\":1000}]}}"},
        };
        TorboxTransport transport =
            [script = std::make_shared<std::vector<std::pair<std::string,
                 std::string>>>(script)]
            (const TorboxHttpRequest& req, TorboxHttpResponse& res,
             std::string&) mutable {
                for (auto it = script->begin(); it != script->end(); ++it)
                    if (req.url.find(it->first) != std::string::npos) {
                        res.status = 200; res.body = it->second;
                        if (it->first == std::string("createtorrent"))
                            script->erase(it);
                        return true;
                    }
                res.status = 200;
                res.body = "{\"success\":false,\"detail\":\"unexpected\"}";
                return true;
            };

        CatalogEntry tb = entry;
        tb.title = "Game";
        tb.magnetUri = "magnet:?xt=urn:btih:deadbeef";

        CatalogBatchInstaller installer(
            root, [](const CatalogEntry&, const std::string&,
                     std::atomic<bool>&,
                     const MagnetResolver::ProgressCallback&,
                     std::vector<uint8_t>&, std::string&) { return false; });
        std::atomic<bool> cancelled{false};
        DebridBatchTiming fast{1, 1000};
        TorboxProvider provider("key", transport);
        BatchPreparation prepared = installer.prepareViaDebrid(
            {tb}, StreamSelection::PackagesOnly, provider, cancelled, {},
            fast);

        assert(prepared.failures().empty());
        assert(prepared.items().size() == 1);
        assert(prepared.items()[0].source == InstallSource::Debrid);
        assert(prepared.items()[0].debridId == "7");
        assert(prepared.items()[0].mode == TransferMode::StreamInstall);
        assert(prepared.items()[0].space.packageFiles == 1);
        assert(prepared.items()[0].preview.name == "Game");
        assert(prepared.items()[0].torrentPath.empty());
    }

    {
        // Debrid enqueue: one selected item imports; one deselected is removed.
        std::atomic<int> removeCalls{0};
        TorboxTransport transport =
            [&removeCalls](const TorboxHttpRequest& req,
                           TorboxHttpResponse& res, std::string&) {
                if (req.url.find("controltorrent") != std::string::npos)
                    ++removeCalls;
                res.status = 200; res.body = "{\"success\":true}";
                return true;
            };

        BatchPreparation prepared;
        {
            PreparedCatalogInstall keep;
            keep.entry = entry; keep.entry.infoHash = entry.infoHash;
            keep.preview.name = "Keep"; keep.preview.totalBytes = 1000;
            keep.mode = TransferMode::DownloadOnly;
            keep.source = InstallSource::Debrid; keep.debridId = "11";
            keep.selected = true;
            prepared.items().push_back(std::move(keep));

            PreparedCatalogInstall drop;
            drop.entry = entry; drop.entry.infoHash =
                "00112233445566778899aabbccddeeff00112233";
            drop.preview.name = "Drop"; drop.preview.totalBytes = 2000;
            drop.mode = TransferMode::DownloadOnly;
            drop.source = InstallSource::Debrid; drop.debridId = "22";
            drop.selected = false;
            prepared.items().push_back(std::move(drop));
        }

        const std::string appRoot = std::string(root) + "/tbapp";
        DownloadManager manager(appRoot, false);
        CatalogBatchInstaller installer(
            root, [](const CatalogEntry&, const std::string&,
                     std::atomic<bool>&,
                     const MagnetResolver::ProgressCallback&,
                     std::vector<uint8_t>&, std::string&) { return false; });
        TorboxProvider provider("key", transport);
        BatchEnqueueResult queued = installer.enqueueViaDebrid(
            prepared, manager, provider);

        assert(queued.taskIds.size() == 1);
        assert(queued.skipped == 1);
        assert(removeCalls.load() == 1);
        assert(manager.snapshot().size() == 1);
        assert(manager.snapshot()[0].source == TaskSource::Debrid);
        assert(manager.snapshot()[0].debridId == "11");

        std::string rmerr;
        assert(manager.remove(queued.taskIds[0], true, rmerr));
        unlink((appRoot + "/queue.bencode").c_str());
        rmdir((appRoot + "/torrents").c_str());
        rmdir((appRoot + "/downloads").c_str());
        rmdir(appRoot.c_str());
    }

    rmdir(root);
    std::puts("batch install tests passed");
    return 0;
}
