#include "obs-integration.hpp"
#include "plugin-support.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.h>

namespace twitch_bench {

// ─────────────────────────────────────────────────────────────────────────────
// Helper: obtain the active obs_service_t* from the frontend.
// NOTE: obs_frontend_get_streaming_service() calls OBSBasic::GetService() which
// returns a raw pointer with NO addref. Do NOT call obs_service_release() on it.
// ─────────────────────────────────────────────────────────────────────────────

static obs_service_t *getActiveService()
{
    return obs_frontend_get_streaming_service();
}

// ─────────────────────────────────────────────────────────────────────────────
// currentStreamServer
// ─────────────────────────────────────────────────────────────────────────────

std::string OBSIntegration::currentStreamServer()
{
    obs_service_t *svc = getActiveService();
    if (!svc) return {};

    obs_data_t *settings = obs_service_get_settings(svc);
    const char *server   = obs_data_get_string(settings, "server");
    std::string result   = server ? server : "";

    obs_data_release(settings);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// currentStreamKey
// ─────────────────────────────────────────────────────────────────────────────

std::string OBSIntegration::currentStreamKey()
{
    obs_service_t *svc = getActiveService();
    if (!svc) return {};

    obs_data_t *settings = obs_service_get_settings(svc);
    const char *key      = obs_data_get_string(settings, "key");
    std::string result   = key ? key : "";

    obs_data_release(settings);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// applyStreamServer
//
// Updates "server" in the live service settings object. No obs_service_release
// because obs_frontend_get_streaming_service returns a non-owned pointer.
// obs_service_get_settings addrefs; we release that ref after writing.
// ─────────────────────────────────────────────────────────────────────────────

bool OBSIntegration::applyStreamServer(const std::string &rtmpUrl)
{
    if (rtmpUrl.empty()) {
        TLOG_ERROR("applyStreamServer: empty URL");
        return false;
    }

    obs_service_t *svc = getActiveService();
    if (!svc) {
        TLOG_ERROR("applyStreamServer: no active streaming service");
        return false;
    }

    obs_data_t *settings = obs_service_get_settings(svc);
    if (!settings) {
        TLOG_ERROR("applyStreamServer: could not get service settings");
        return false;
    }

    obs_data_set_string(settings, "server", rtmpUrl.c_str());
    obs_data_release(settings);

    // Persist the updated service settings so OBS saves them cleanly on
    // shutdown. Without this, OBS's shutdown path sees the in-memory settings
    // as dirty and may not flush them correctly, causing the "improper
    // shutdown" warning.
    obs_frontend_save_streaming_service();

    TLOG_INFO("applyStreamServer: applied '%s'", rtmpUrl.c_str());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// isStreaming
// ─────────────────────────────────────────────────────────────────────────────

bool OBSIntegration::isStreaming()
{
    return obs_frontend_streaming_active();
}

} // namespace twitch_bench
