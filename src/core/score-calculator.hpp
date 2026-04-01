#pragma once

#include "twitch/ingest-fetcher.hpp"
#include <vector>
#include <cstdint>

namespace twitch_bench {

// ─────────────────────────────────────────────────────────────────────────────
// ServerResult — benchmark results for one ingest server
// ─────────────────────────────────────────────────────────────────────────────

struct ServerResult {
    IngestServer server;

    // DNS
    int64_t dnsLatencyMs  = -1;   // -1 = failed
    bool    dnsSuccess    = false;

    // TCP across N rounds
    int     tcpRounds      = 0;
    int     tcpSuccesses   = 0;
    int64_t tcpMinMs       = -1;
    int64_t tcpMaxMs       = -1;
    int64_t tcpMeanMs      = -1;
    int64_t tcpJitterMs    = -1;   // std-dev of successful latencies

    // Derived
    double  score          = 0.0;  // higher = better
    bool    recommended    = false;

    // Human-readable status
    std::string statusNote;
};

// ─────────────────────────────────────────────────────────────────────────────
// ScoreCalculator
// ─────────────────────────────────────────────────────────────────────────────

class ScoreCalculator {
public:
    // Compute and assign `score` for every entry in `results`.
    // Also sets `recommended = true` on the single best server.
    static void compute(std::vector<ServerResult> &results);

private:
    // Individual component weights (must sum to 1.0)
    static constexpr double W_LATENCY     = 0.40;
    static constexpr double W_JITTER      = 0.25;
    static constexpr double W_RELIABILITY = 0.25;
    static constexpr double W_DNS         = 0.10;

    // A server with fewer than this many successful TCP rounds gets a 0 score
    static constexpr int MIN_SUCCESS_ROUNDS = 1;
};

} // namespace twitch_bench
