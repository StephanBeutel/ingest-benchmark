#include "score-calculator.hpp"
#include "plugin-support.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace twitch_bench {

// ─────────────────────────────────────────────────────────────────────────────
// Scoring model
//
// Each server receives a score in [0, 100].  The score is a weighted sum of
// four normalised sub-scores, each also in [0, 1]:
//
//   reliability_score = tcpSuccesses / tcpRounds
//     — Primary gate: servers that fail often score low regardless of latency.
//
//   latency_score = 1 - (meanTcpMs - minInSet) / (maxInSet - minInSet + 1)
//     — Inverted & normalised so the fastest server gets 1.0.
//
//   jitter_score = 1 - (jitterMs / maxJitterInSet + 1)
//     — Stable servers score higher.
//
//   dns_score = 1 - (dnsMs - minDnsInSet) / (maxDnsInSet - minDnsInSet + 1)
//     — Fast DNS is a small bonus.
//
// Final score = 100 * (W_LATENCY * latency + W_JITTER * jitter +
//                      W_RELIABILITY * reliability + W_DNS * dns)
// ─────────────────────────────────────────────────────────────────────────────

void ScoreCalculator::compute(std::vector<ServerResult> &results)
{
    if (results.empty()) return;

    // ── Collect ranges for normalisation ────────────────────────────────────
    int64_t minTcpMs  = std::numeric_limits<int64_t>::max();
    int64_t maxTcpMs  = 0;
    int64_t minDnsMs  = std::numeric_limits<int64_t>::max();
    int64_t maxDnsMs  = 0;
    int64_t maxJitter = 0;

    for (const auto &r : results) {
        if (r.tcpSuccesses >= MIN_SUCCESS_ROUNDS && r.tcpMeanMs >= 0) {
            minTcpMs = std::min(minTcpMs, r.tcpMeanMs);
            maxTcpMs = std::max(maxTcpMs, r.tcpMeanMs);
        }
        if (r.dnsSuccess && r.dnsLatencyMs >= 0) {
            minDnsMs = std::min(minDnsMs, r.dnsLatencyMs);
            maxDnsMs = std::max(maxDnsMs, r.dnsLatencyMs);
        }
        if (r.tcpJitterMs >= 0)
            maxJitter = std::max(maxJitter, r.tcpJitterMs);
    }

    // Guard against degenerate ranges
    if (minTcpMs > maxTcpMs) { minTcpMs = 0; maxTcpMs = 1; }
    if (minDnsMs > maxDnsMs) { minDnsMs = 0; maxDnsMs = 1; }
    if (maxJitter == 0)        maxJitter = 1;

    int64_t tcpRange = maxTcpMs - minTcpMs + 1;
    int64_t dnsRange = maxDnsMs - minDnsMs + 1;

    // ── Assign scores ────────────────────────────────────────────────────────
    double bestScore = -1.0;
    ServerResult *bestResult = nullptr;

    for (auto &r : results) {
        if (r.tcpRounds == 0 || r.tcpSuccesses < MIN_SUCCESS_ROUNDS) {
            r.score      = 0.0;
            r.statusNote = "No successful connections";
            continue;
        }

        // reliability
        double reliability = static_cast<double>(r.tcpSuccesses) /
                             static_cast<double>(r.tcpRounds);

        // latency (inverted — lower is better)
        double latency = (r.tcpMeanMs >= 0)
            ? 1.0 - static_cast<double>(r.tcpMeanMs - minTcpMs) /
                    static_cast<double>(tcpRange)
            : 0.0;

        // jitter (inverted — lower is better)
        double jitter = (r.tcpJitterMs >= 0)
            ? 1.0 - static_cast<double>(r.tcpJitterMs) /
                    static_cast<double>(maxJitter + 1)
            : 0.5;  // unknown jitter → neutral

        // dns
        double dns = (r.dnsSuccess && r.dnsLatencyMs >= 0)
            ? 1.0 - static_cast<double>(r.dnsLatencyMs - minDnsMs) /
                    static_cast<double>(dnsRange)
            : 0.0;

        r.score = 100.0 * (W_LATENCY     * latency +
                           W_JITTER      * jitter  +
                           W_RELIABILITY * reliability +
                           W_DNS         * dns);

        // Clamp to [0, 100]
        r.score = std::max(0.0, std::min(100.0, r.score));

        if (r.score > bestScore) {
            bestScore  = r.score;
            bestResult = &r;
        }
    }

    if (bestResult) {
        bestResult->recommended = true;
        TLOG_INFO("Best server: %s (score=%.1f mean=%lldms jitter=%lldms)",
                  bestResult->server.name.c_str(),
                  bestResult->score,
                  (long long)bestResult->tcpMeanMs,
                  (long long)bestResult->tcpJitterMs);
    }
}

} // namespace twitch_bench
