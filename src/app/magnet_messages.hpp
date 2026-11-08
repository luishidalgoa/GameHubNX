#pragma once

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>

namespace gamehubnx {

/* Phrases of the no-connection messages. catalog_helpers.hpp matches on them to pick
   a card badge, so they live here, next to the wording that produces them. */
inline constexpr const char* kPhraseRefused = "refused the connection";
inline constexpr const char* kPhraseCouldNotReach = "could not reach any of them";
inline constexpr const char* kPhraseMayBlock = "may block BitTorrent";
inline constexpr const char* kPhraseLocal = "could not open the connections";

/* What one failed connect says about the network. */
enum class ConnectOutcome { Refused, TimedOut, Local, Unreachable };

/* `failure` is the stage PeerAttempt recorded ("connect refused", "connect timed out"
   or "connect") and `error` its errno. A refusal means the peer answered. A timeout
   means it did not. A resource error means this console could not open the socket,
   which says nothing about the network. Anything else is treated as unreachable. */
inline ConnectOutcome classifyConnectFailure(const char* failure, int error) {
    if (failure && std::strcmp(failure, "connect timed out") == 0)
        return ConnectOutcome::TimedOut;
    switch (error) {
    case ECONNREFUSED:
        return ConnectOutcome::Refused;
    case ETIMEDOUT:
        return ConnectOutcome::TimedOut;
    case ENOBUFS:
    case ENOMEM:
    case EMFILE:
    case ENFILE:
        return ConnectOutcome::Local;
    default:
        return ConnectOutcome::Unreachable;
    }
}

/* The message shown when no peer accepted a TCP session. The counts are connect
   attempts by outcome, not distinct peers. `trackersEmpty` means a tracker answered
   with an empty list and no tracker listed any peer; a silent tracker does not count.
   The wording
   blames the network only when the evidence points at it. */
inline std::string describeConnectFailures(uint32_t peerCount, uint32_t refused,
                                           uint32_t timedOut, uint32_t unreachable,
                                           uint32_t local, bool trackersEmpty) {
    std::string text;
    if (trackersEmpty)
        text += "The trackers listed no peers for this torrent. ";
    const std::string found = "Found " + std::to_string(peerCount) + " peers";
    if (refused + timedOut + unreachable + local == 0) {
        text += found + ", but no connection was attempted before the time limit. "
                        "Try again.";
    } else if (local && !refused && !timedOut && !unreachable) {
        text += found + ", but this console " + kPhraseLocal +
                " (it is out of network sockets). Close other network activity, such as "
                "downloads or the FTP server, and try again.";
    } else if (unreachable && !refused && !timedOut && !local) {
        text += found + ", but the network " + kPhraseCouldNotReach +
                ". Try another Wi-Fi or a phone hotspot.";
    } else if (refused && !timedOut && !unreachable && !local) {
        text += found + ", but all of them " + kPhraseRefused +
                ": they are offline or not accepting connections. Try again later.";
    } else if (timedOut && !refused && !unreachable && !local) {
        text += found + ", but none answered in time. They may be offline, or this "
                        "network " + std::string(kPhraseMayBlock) +
                ": try again later or on another Wi-Fi or a phone hotspot.";
    } else {
        text += found + ", but none accepted a connection (refused, unreachable, no answer "
                        "or out of sockets). Try again later; if it keeps happening, try "
                        "another Wi-Fi or a phone hotspot.";
    }
    return text;
}

} // namespace gamehubnx
