#pragma once

// Convenience wrapper around OBS blog() that prepends the plugin name.
// Include this header in every translation unit that needs logging.

#include <obs-module.h>

#define PLUGIN_NAME "obs-twitch-ingest-benchmark"

// blog wrappers — use these instead of bare blog()
#define TLOG_INFO(fmt, ...)  blog(LOG_INFO,    "[" PLUGIN_NAME "] " fmt, ##__VA_ARGS__)
#define TLOG_WARN(fmt, ...)  blog(LOG_WARNING, "[" PLUGIN_NAME "] " fmt, ##__VA_ARGS__)
#define TLOG_ERROR(fmt, ...) blog(LOG_ERROR,   "[" PLUGIN_NAME "] " fmt, ##__VA_ARGS__)
#define TLOG_DEBUG(fmt, ...) blog(LOG_DEBUG,   "[" PLUGIN_NAME "] " fmt, ##__VA_ARGS__)
