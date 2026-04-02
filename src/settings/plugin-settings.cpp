#include "plugin-settings.hpp"
#include "plugin-support.h"

#include <util/config-file.h>   // OBS config_get_bool / config_set_bool etc.
#include <obs-module.h>

// OBS exposes the global config via obs_frontend_get_global_config() only in
// the frontend API.  We include it here; the linker will find it because the
// plugin links against obs-frontend-api.
#include <obs-frontend-api.h>

namespace twitch_bench {

static const char *SECTION = "TwitchBenchmark";

PluginSettings &PluginSettings::instance()
{
    static PluginSettings s;
    return s;
}

void PluginSettings::load()
{
    config_t *cfg = obs_frontend_get_global_config();
    if (!cfg) {
        TLOG_WARN("load(): global config not available, using defaults");
        return;
    }

    config_set_default_bool (cfg, SECTION, "AutoBenchmark",   false);
    config_set_default_bool (cfg, SECTION, "EUOnly",          false);
    config_set_default_bool (cfg, SECTION, "StartAfter",      false);
    config_set_default_int  (cfg, SECTION, "CacheTTL",        300);
    config_set_default_int  (cfg, SECTION, "ProbeRounds",     3);
    config_set_default_int  (cfg, SECTION, "ProbeTimeoutMs",  3000);

    m_autoBenchmark   = config_get_bool(cfg, SECTION, "AutoBenchmark");
    m_euOnly          = config_get_bool(cfg, SECTION, "EUOnly");
    m_startAfter      = config_get_bool(cfg, SECTION, "StartAfter");
    m_cacheTtl        = (int)config_get_int(cfg, SECTION, "CacheTTL");
    m_probeRounds     = (int)config_get_int(cfg, SECTION, "ProbeRounds");
    m_probeTimeoutMs  = (int)config_get_int(cfg, SECTION, "ProbeTimeoutMs");

    TLOG_INFO("Settings loaded — autoBenchmark=%d euOnly=%d startAfter=%d "
              "cacheTtl=%ds probeRounds=%d probeTimeoutMs=%d",
              m_autoBenchmark, m_euOnly, m_startAfter,
              m_cacheTtl, m_probeRounds, m_probeTimeoutMs);
}

void PluginSettings::save() const
{
    config_t *cfg = obs_frontend_get_global_config();
    if (!cfg) return;

    config_set_bool(cfg, SECTION, "AutoBenchmark",   m_autoBenchmark);
    config_set_bool(cfg, SECTION, "EUOnly",          m_euOnly);
    config_set_bool(cfg, SECTION, "StartAfter",      m_startAfter);
    config_set_int (cfg, SECTION, "CacheTTL",        m_cacheTtl);
    config_set_int (cfg, SECTION, "ProbeRounds",     m_probeRounds);
    config_set_int (cfg, SECTION, "ProbeTimeoutMs",  m_probeTimeoutMs);

    config_save_safe(cfg, "tmp", nullptr);
}

void PluginSettings::setAutoBenchmarkBeforeStream(bool v)
{
    m_autoBenchmark = v;
    save();
}

void PluginSettings::setEuOnlyFilter(bool v)
{
    m_euOnly = v;
    save();
}

void PluginSettings::setStartStreamAfterBenchmark(bool v)
{
    m_startAfter = v;
    save();
}

void PluginSettings::setCacheTtlSeconds(int v)
{
    m_cacheTtl = v;
    save();
}

void PluginSettings::setProbeRounds(int v)
{
    m_probeRounds = v;
    save();
}

void PluginSettings::setProbeTimeoutMs(int v)
{
    m_probeTimeoutMs = v;
    save();
}

} // namespace twitch_bench
