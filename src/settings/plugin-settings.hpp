#pragma once

#include <string>
#include <cstdint>

// Persistent plugin configuration.
// Values are stored in OBS's global config under the section "TwitchBenchmark".
// All access is safe to call from the main thread only.

namespace twitch_bench {

class PluginSettings {
public:
    // Singleton accessor
    static PluginSettings &instance();

    // Load from OBS global config.  Call once after obs_module_load().
    void load();

    // Persist back to OBS global config.
    void save() const;

    // ── options ──────────────────────────────────────────────────────────────

    bool  autoBenchmarkBeforeStream() const { return m_autoBenchmark; }
    void  setAutoBenchmarkBeforeStream(bool v);

    bool  euOnlyFilter() const { return m_euOnly; }
    void  setEuOnlyFilter(bool v);

    bool  startStreamAfterBenchmark() const { return m_startAfter; }
    void  setStartStreamAfterBenchmark(bool v);

    // How many seconds a cached benchmark result stays valid (0 = always re-run)
    int   cacheTtlSeconds() const { return m_cacheTtl; }
    void  setCacheTtlSeconds(int v);

    // Number of TCP probe rounds per server
    int   probeRounds() const { return m_probeRounds; }
    void  setProbeRounds(int v);

    // Per-probe TCP timeout in milliseconds
    int   probeTimeoutMs() const { return m_probeTimeoutMs; }
    void  setProbeTimeoutMs(int v);

private:
    PluginSettings() = default;

    bool     m_autoBenchmark   = false;
    bool     m_euOnly          = false;
    bool     m_startAfter      = false;
    int      m_cacheTtl        = 300;  // 5 minutes
    int      m_probeRounds     = 3;
    int      m_probeTimeoutMs  = 3000;
};

} // namespace twitch_bench
