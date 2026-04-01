#pragma once

#include <string>
#include <cstdint>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// NetProbe — synchronous, blocking probes.
// These are designed to be called from a worker thread, not the OBS main thread.
// ─────────────────────────────────────────────────────────────────────────────

namespace twitch_bench {

// Result of a single DNS lookup
struct DnsResult {
    bool    success     = false;
    int64_t latencyMs   = -1;    // -1 = failed / timed out
    std::string resolvedIp;      // first address resolved
};

// Result of a single TCP connection attempt
struct TcpResult {
    bool    success     = false;
    int64_t latencyMs   = -1;    // -1 = failed / timed out
};

class NetProbe {
public:
    // Resolve `hostname` via getaddrinfo and return timing.
    // `timeoutMs` is the hard deadline; if resolution takes longer the result
    // is marked as failed (note: POSIX getaddrinfo is blocking and doesn't
    // natively support a timeout, so we implement one via a thread + condition
    // variable on Windows and POSIX alike).
    static DnsResult probeDns(const std::string &hostname, int timeoutMs = 3000);

    // Attempt a TCP connection to `hostname:port`.
    // Uses a non-blocking socket + select() for reliable cross-platform timeout.
    static TcpResult probeTcp(const std::string &hostname, uint16_t port,
                               int timeoutMs = 3000);

    // Run `rounds` TCP probes and return all results.
    static std::vector<TcpResult> probeTcpMulti(const std::string &hostname,
                                                  uint16_t port,
                                                  int rounds,
                                                  int timeoutMs = 3000);

    // ── Platform initialisation ──────────────────────────────────────────────
    // Must be called once before any probes (no-op on non-Windows).
    static bool initPlatform();
    static void cleanupPlatform();
};

} // namespace twitch_bench
