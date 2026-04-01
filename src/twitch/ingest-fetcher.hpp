#pragma once

#include <string>
#include <vector>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// IngestServer — one Twitch ingest endpoint as parsed from the API response
// ─────────────────────────────────────────────────────────────────────────────

namespace twitch_bench {

struct IngestServer {
    std::string name;        // e.g. "EU Frankfurt"
    std::string urlTemplate; // e.g. "rtmp://fra02.contribute.live-video.net/app/{stream_key}"
    std::string host;        // extracted hostname, e.g. "fra02.contribute.live-video.net"
    uint16_t    port = 1935; // RTMP default
    bool        isDefault = false;
    int         priority  = 0;

    // Inferred region tag, e.g. "EU", "US", "AP"
    std::string region;
};

// ─────────────────────────────────────────────────────────────────────────────
// IngestFetcher — downloads and parses the Twitch ingest list
// ─────────────────────────────────────────────────────────────────────────────

class IngestFetcher {
public:
    using ProgressCallback = std::function<void(const std::string &status)>;

    // Synchronous fetch + parse.  Must be called from a worker thread.
    // Returns an empty vector on failure.
    static std::vector<IngestServer> fetch(ProgressCallback cb = nullptr);

    // Filter helpers
    static std::vector<IngestServer> filterEU(const std::vector<IngestServer> &all);

private:
    static std::string httpGet(const std::string &url, int timeoutMs = 10000);
    static std::vector<IngestServer> parseJson(const std::string &json);
    static std::string extractHost(const std::string &url);
    static std::string inferRegion(const std::string &name);
};

} // namespace twitch_bench
