#include <obs-module.h>
#include <obs-frontend-api.h>

#include "plugin-support.h"
#include "settings/plugin-settings.hpp"
#include "ui/benchmark-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name()
{
    return "Twitch Ingest Benchmark";
}

MODULE_EXPORT const char *obs_module_description()
{
    return "Benchmarks Twitch ingest servers and selects the fastest one.";
}

static twitch_bench::BenchmarkDock *g_dock = nullptr;

static void onFrontendEvent(enum obs_frontend_event event, void * /*data*/)
{
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        twitch_bench::PluginSettings::instance().load();
        TLOG_INFO("Frontend finished loading — settings applied");

        // Install the event filter on OBS's stream button now that the
        // main window is fully constructed.
        if (g_dock)
            g_dock->installStreamButtonFilter();
    }
}

bool obs_module_load()
{
    TLOG_INFO("Loading version " PLUGIN_VERSION);
    obs_frontend_add_event_callback(onFrontendEvent, nullptr);
    return true;
}

void obs_module_post_load()
{
    TLOG_INFO("Post-load: creating dock widget");
    g_dock = new twitch_bench::BenchmarkDock();
    obs_frontend_add_dock_by_id(
        "twitch-ingest-benchmark-dock",
        "Twitch Ingest Benchmark",
        g_dock);
    TLOG_INFO("Dock registered");
}

void obs_module_unload()
{
    obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
    g_dock = nullptr;
    TLOG_INFO("Unloaded");
}
