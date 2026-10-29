// The saved queue keeps only downloads from GameHub. Torrent tasks and tasks
// from third-party debrid services are dropped when the queue loads.
#include "app/download_manager.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace gamehubnx;

namespace {

std::string bstr(const std::string& value) {
    return std::to_string(value.size()) + ":" + value;
}

std::string task(const std::string& id, const std::string& source,
                 const std::string& debridId) {
    std::string item = "d";
    item += "2:id" + bstr(id);
    item += "4:name" + bstr("Example " + id);
    item += "8:metainfo0:";
    item += "4:data" + bstr("/tmp/gamehubnx-direct-queue/downloads/" + id);
    item += "6:status" + bstr("queued");
    item += "5:total" + std::string("i1000e");
    item += "6:source" + bstr(source);
    item += "9:debrid-id" + bstr(debridId);
    item += "e";
    return item;
}

}  // namespace

int main() {
    const std::string root = "/tmp/gamehubnx-direct-queue";
    mkdir(root.c_str(), 0755);
    mkdir((root + "/downloads").c_str(), 0755);
    const std::string statePath = root + "/queue.bencode";

    const std::string state =
        "d7:versioni6e5:tasksl" +
        task("aaaaaaaa", "torrent", "") +
        task("bbbbbbbb", "debrid", "12345") +
        task("cccccccc", "debrid", "https://gamehub.example/api/shop/download/1/x.nsp") +
        "ee";
    {
        std::ofstream output(statePath, std::ios::binary);
        output << state;
    }

    DownloadManager manager(root, false);
    const auto tasks = manager.snapshot();
    assert(tasks.size() == 1);
    assert(tasks[0].id == "cccccccc");

    unlink(statePath.c_str());
    rmdir((root + "/downloads/aaaaaaaa").c_str());
    rmdir((root + "/downloads").c_str());
    rmdir(root.c_str());
    std::puts("direct queue tests passed");
    return 0;
}
