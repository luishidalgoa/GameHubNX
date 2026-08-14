// GameHub as a DebridProvider: the degenerate case where the download URL is
// already known, so a transfer is Ready on its first poll. The size probe is
// injected, so these run with no network.

#include "app/gamehub_provider.hpp"
#include "app/gamehub_catalog.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace pipensx;

namespace {

const char* const kUrl =
    "https://gamehub.hdglabs.com/api/shop/download/250/"
    "Animal%20Crossing%20New%20Horizons%20%5B01006F8002326000%5D.nsp";
const char* const kOffHost = "https://evil.example.com/api/shop/download/1/x.nsp";

// A probe that always answers, so size handling is exercised without curl.
GameHubProvider::SizeProbe fixedProbe(uint64_t bytes) {
    return [bytes](const std::string&, uint64_t& out, std::string&) {
        out = bytes;
        return true;
    };
}

GameHubProvider::SizeProbe failingProbe() {
    return [](const std::string&, uint64_t&, std::string& err) {
        err = "probe refused";
        return false;
    };
}

void test_create_echoes_the_url() {
    GameHubProvider p{fixedProbe(1024)};
    std::string id, err;
    assert(p.createFromMagnet(kUrl, id, err));
    assert(id == kUrl);          // the handle IS the URL: no state kept anywhere
    assert(err.empty());
}

void test_create_refuses_foreign_and_empty() {
    GameHubProvider p{fixedProbe(1024)};
    std::string id, err;

    assert(!p.createFromMagnet(kOffHost, id, err));
    assert(!err.empty());

    err.clear();
    assert(!p.createFromMagnet("", id, err));
    assert(!err.empty());
}

void test_torrent_files_are_rejected() {
    GameHubProvider p{fixedProbe(1024)};
    std::string id, err;
    // There is no torrent behind a GameHub entry; saying so beats pretending.
    assert(!p.createFromFile("/tmp/whatever.torrent", id, err));
    assert(!err.empty());
}

void test_info_is_ready_immediately() {
    GameHubProvider p{fixedProbe(6694761920ULL)};
    DebridInfo info;
    std::string err;
    assert(p.fetchInfo(kUrl, info, err));

    // Nothing to wait for: the file sits on a server the user owns.
    assert(info.phase == DebridInfo::Phase::Ready);
    assert(info.progress == 1.0);
    assert(info.bytes == 6694761920ULL);
    assert(info.files.size() == 1);
    assert(info.files[0].bytes == 6694761920ULL);
    assert(info.links.size() == 1 && info.links[0] == kUrl);
    // The display name is the decoded file name, not the raw URL.
    assert(info.name.find("Animal Crossing New Horizons") != std::string::npos);
    assert(info.name.find("%20") == std::string::npos);
}

void test_probe_failure_is_not_transfer_failure() {
    GameHubProvider p{failingProbe()};
    DebridInfo info;
    std::string err;
    // A missing size costs a progress bar, never the download.
    assert(p.fetchInfo(kUrl, info, err));
    assert(info.phase == DebridInfo::Phase::Ready);
    assert(info.bytes == 0);
    assert(info.files.size() == 1);
}

void test_info_refuses_foreign_host() {
    GameHubProvider p{fixedProbe(10)};
    DebridInfo info;
    std::string err;
    assert(!p.fetchInfo(kOffHost, info, err));
    assert(!err.empty());
}

void test_resolve_returns_the_same_url() {
    GameHubProvider p{fixedProbe(10)};
    DebridInfo info;
    std::string err;
    assert(p.fetchInfo(kUrl, info, err));

    std::string url;
    assert(p.resolveDownloadUrl(kUrl, info, 0, info.files[0], url, err));
    assert(url == kUrl);

    // And still refuses to hand back anything off-host.
    assert(!p.resolveDownloadUrl(kOffHost, info, 0, info.files[0], url, err));
}

void test_no_op_lifecycle() {
    GameHubProvider p{fixedProbe(10)};
    std::string err;
    // Nothing is created server-side, so selection and removal are trivially
    // true rather than errors — the transfer layer calls them unconditionally.
    assert(p.selectFiles(kUrl, {"0"}, err));
    assert(p.remove(kUrl, err));
    assert(p.validate(err));
    assert(std::string(p.name()) == std::string(gamehub::kSourceLabel));
}

} // namespace

int main() {
    test_create_echoes_the_url();
    test_create_refuses_foreign_and_empty();
    test_torrent_files_are_rejected();
    test_info_is_ready_immediately();
    test_probe_failure_is_not_transfer_failure();
    test_info_refuses_foreign_host();
    test_resolve_returns_the_same_url();
    test_no_op_lifecycle();
    std::printf("gamehub provider tests passed\n");
    return 0;
}
