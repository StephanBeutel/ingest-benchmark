#include "ingest-fetcher.hpp"
#include "plugin-support.h"

// HTTPS via libcurl — always present on macOS as a system library.
// This avoids depending on Qt's TLS backend plugin, which OBS does not ship.
#include <curl/curl.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QString>

#include <algorithm>
#include <cctype>

namespace twitch_bench {

static const char *INGEST_URL = "https://ingest.twitch.tv/ingests";

// ─────────────────────────────────────────────────────────────────────────────
// HTTPS GET via libcurl (system library, always present on macOS)
// ─────────────────────────────────────────────────────────────────────────────

static size_t curlWriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *buf = static_cast<std::string *>(userdata);
    buf->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string IngestFetcher::httpGet(const std::string &url, int timeoutMs)
{
    std::string body;

    CURL *curl = curl_easy_init();
    if (!curl) {
        TLOG_ERROR("httpGet: curl_easy_init() failed");
        return {};
    }

    std::string userAgent = "ingest-benchmark-for-twitch/" PLUGIN_VERSION;

    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT,       userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,   curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,       &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,      (long)timeoutMs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,  1L);
    // Use system CA bundle (macOS Keychain via SecureTransport)
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,  1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,  2L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        TLOG_WARN("httpGet: curl error: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return {};
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (httpCode < 200 || httpCode >= 300) {
        TLOG_WARN("httpGet: HTTP %ld for %s", httpCode, url.c_str());
        return {};
    }

    return body;
}

// ─────────────────────────────────────────────────────────────────────────────
// Extract hostname from an RTMP URL
// Input:  "rtmp://fra02.contribute.live-video.net/app/{stream_key}"
// Output: "fra02.contribute.live-video.net"
// ─────────────────────────────────────────────────────────────────────────────

std::string IngestFetcher::extractHost(const std::string &url)
{
    // Strip scheme
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return url;
    std::string rest = url.substr(schemeEnd + 3);

    // Take everything up to the first '/'
    size_t slash = rest.find('/');
    return (slash != std::string::npos) ? rest.substr(0, slash) : rest;
}

// ─────────────────────────────────────────────────────────────────────────────
// Infer a short region tag from the server's display name
// ─────────────────────────────────────────────────────────────────────────────

std::string IngestFetcher::inferRegion(const std::string &name)
{
    // Convert to lower for matching
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower.find("europe")   != std::string::npos ||
        lower.find(" eu ")     != std::string::npos ||
        lower.find("frankfurt")!= std::string::npos ||
        lower.find("paris")    != std::string::npos ||
        lower.find("amsterdam")!= std::string::npos ||
        lower.find("london")   != std::string::npos ||
        lower.find("stockholm")!= std::string::npos ||
        lower.find("madrid")   != std::string::npos ||
        lower.find("milan")    != std::string::npos ||
        lower.find("warsaw")   != std::string::npos ||
        lower.find("prague")   != std::string::npos)
        return "EU";

    if (lower.find("us ")      != std::string::npos ||
        lower.find("united states") != std::string::npos ||
        lower.find("virginia") != std::string::npos ||
        lower.find("oregon")   != std::string::npos ||
        lower.find("ohio")     != std::string::npos)
        return "US";

    if (lower.find("asia")     != std::string::npos ||
        lower.find("japan")    != std::string::npos ||
        lower.find("korea")    != std::string::npos ||
        lower.find("singapore")!= std::string::npos ||
        lower.find("india")    != std::string::npos ||
        lower.find("mumbai")   != std::string::npos)
        return "APAC";

    if (lower.find("australia")!= std::string::npos ||
        lower.find("sydney")   != std::string::npos)
        return "AU";

    if (lower.find("brazil")   != std::string::npos ||
        lower.find("são paulo")!= std::string::npos ||
        lower.find("sao paulo")!= std::string::npos)
        return "SA";

    return "Global";
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse the JSON returned by https://ingest.twitch.tv/ingests
//
// Expected format:
// {
//   "ingests": [
//     {
//       "name": "EU Frankfurt",
//       "default": false,
//       "url_template": "rtmp://fra02.contribute.live-video.net/app/{stream_key}",
//       "priority": 10
//     }, ...
//   ]
// }
// ─────────────────────────────────────────────────────────────────────────────

std::vector<IngestServer> IngestFetcher::parseJson(const std::string &json)
{
    std::vector<IngestServer> servers;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(json), &err);

    if (err.error != QJsonParseError::NoError) {
        TLOG_ERROR("JSON parse error: %s",
                   err.errorString().toUtf8().constData());
        return servers;
    }

    QJsonObject root = doc.object();
    QJsonArray ingests = root["ingests"].toArray();

    for (const QJsonValue &val : ingests) {
        QJsonObject obj = val.toObject();

        IngestServer s;
        s.name        = std::string(obj["name"].toString().toUtf8().constData());
        s.isDefault   = obj["default"].toBool(false);
        s.priority    = obj["priority"].toInt(0);

        // Prefer "url_template" key; fall back to "url"
        QString urlTemplate = obj["url_template"].toString();
        if (urlTemplate.isEmpty())
            urlTemplate = obj["url"].toString();

        s.urlTemplate = std::string(urlTemplate.toUtf8().constData());
        s.host        = extractHost(s.urlTemplate);
        s.region      = inferRegion(s.name);
        s.port        = 1935;

        if (!s.host.empty())
            servers.push_back(std::move(s));
    }

    TLOG_INFO("Parsed %zu ingest servers", servers.size());
    return servers;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public: fetch()
// ─────────────────────────────────────────────────────────────────────────────

std::vector<IngestServer> IngestFetcher::fetch(ProgressCallback cb)
{
    if (cb) cb("Fetching Twitch ingest list…");
    TLOG_INFO("Fetching ingest list from %s", INGEST_URL);

    std::string body = httpGet(INGEST_URL);
    if (body.empty()) {
        if (cb) cb("Failed to fetch ingest list");
        return {};
    }

    if (cb) cb("Parsing ingest list…");
    return parseJson(body);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public: filterEU()
// ─────────────────────────────────────────────────────────────────────────────

std::vector<IngestServer> IngestFetcher::filterEU(const std::vector<IngestServer> &all)
{
    std::vector<IngestServer> eu;
    for (const auto &s : all)
        if (s.region == "EU")
            eu.push_back(s);
    return eu;
}

} // namespace twitch_bench
