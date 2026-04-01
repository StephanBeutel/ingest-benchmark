#pragma once

#include <string>
#include <optional>

namespace twitch_bench {

// ─────────────────────────────────────────────────────────────────────────────
// OBS stream service integration.
// All functions must be called from the OBS main thread (Qt main thread).
// ─────────────────────────────────────────────────────────────────────────────

class OBSIntegration {
public:
    // Returns the current stream server URL, or empty string if unavailable.
    static std::string currentStreamServer();

    // Sets the stream server URL on the active streaming service.
    // Persists the change via obs_frontend_save_streaming_service().
    // Returns false if the service could not be updated.
    static bool applyStreamServer(const std::string &rtmpUrl);

    // Returns the current stream key (for display / diagnostic purposes only).
    static std::string currentStreamKey();

    static bool isStreaming();
};

} // namespace twitch_bench
