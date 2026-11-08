// The connect-failure wording must blame the network only when the evidence points at
// it: a refusal means the peer answered, and a resource error is this console's own.
#include "app/magnet_messages.hpp"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <string>

using gamehubnx::ConnectOutcome;
using gamehubnx::classifyConnectFailure;
using gamehubnx::describeConnectFailures;

static bool has(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

int main() {
    // Each stage and errno lands in exactly one bucket.
    assert(classifyConnectFailure("connect refused", ECONNREFUSED) == ConnectOutcome::Refused);
    assert(classifyConnectFailure("connect", ECONNREFUSED) == ConnectOutcome::Refused);
    assert(classifyConnectFailure("connect timed out", 0) == ConnectOutcome::TimedOut);
    assert(classifyConnectFailure("connect refused", ETIMEDOUT) == ConnectOutcome::TimedOut);
    assert(classifyConnectFailure("connect", ENOBUFS) == ConnectOutcome::Local);
    assert(classifyConnectFailure("connect", EMFILE) == ConnectOutcome::Local);
    assert(classifyConnectFailure("connect refused", EHOSTUNREACH) == ConnectOutcome::Unreachable);
    assert(classifyConnectFailure("connect", ENETUNREACH) == ConnectOutcome::Unreachable);

    // Every attempt refused: the peers answered, so the network is not the cause.
    std::string refused = describeConnectFailures(2, 5, 0, 0, 0, false);
    assert(has(refused, gamehubnx::kPhraseRefused));
    assert(!has(refused, "block BitTorrent"));
    assert(!has(refused, "Wi-Fi"));

    // Only timeouts: the one case that may point at the network.
    std::string timeouts = describeConnectFailures(3, 0, 4, 0, 0, false);
    assert(has(timeouts, "none answered in time"));
    assert(has(timeouts, gamehubnx::kPhraseMayBlock));

    // The network itself could not route to the peers.
    std::string unreachable = describeConnectFailures(2, 0, 0, 3, 0, false);
    assert(has(unreachable, gamehubnx::kPhraseCouldNotReach));
    assert(has(unreachable, "phone hotspot"));

    // This console ran out of sockets: that is not the network, so no Wi-Fi advice.
    std::string local = describeConnectFailures(2, 0, 0, 0, 4, false);
    assert(has(local, gamehubnx::kPhraseLocal));
    assert(!has(local, "could not reach"));
    assert(!has(local, "Wi-Fi"));

    // A tracker answered with no peers: say so before the peer detail.
    std::string empty = describeConnectFailures(2, 2, 0, 0, 0, true);
    assert(has(empty, "The trackers listed no peers for this torrent."));
    assert(has(empty, gamehubnx::kPhraseRefused));

    // Nothing was attempted (cancelled, or out of time before the first connect).
    std::string none = describeConnectFailures(2, 0, 0, 0, 0, false);
    assert(has(none, "no connection was attempted"));

    // Mixed outcomes: no single cause is blamed.
    std::string mixed = describeConnectFailures(2, 1, 1, 0, 0, false);
    assert(has(mixed, "none accepted a connection"));
    assert(!has(mixed, "all of them"));

    std::printf("test_magnet_messages: ok\n");
    return 0;
}
