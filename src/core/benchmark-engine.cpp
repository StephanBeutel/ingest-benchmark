#include "benchmark-engine.hpp"
#include "network/net-probe.hpp"
#include "twitch/ingest-fetcher.hpp"
#include "plugin-support.h"

#include <QMetaType>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <ctime>

namespace twitch_bench {

// ─────────────────────────────────────────────────────────────────────────────
// Static registration of custom metatypes (called once)
// ─────────────────────────────────────────────────────────────────────────────

static void registerMetaTypes()
{
    static bool registered = false;
    if (!registered) {
        qRegisterMetaType<twitch_bench::ServerResult>("twitch_bench::ServerResult");
        qRegisterMetaType<QList<twitch_bench::ServerResult>>(
            "QList<twitch_bench::ServerResult>");
        registered = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// BenchmarkWorker
// ─────────────────────────────────────────────────────────────────────────────

BenchmarkWorker::BenchmarkWorker(QObject *parent)
    : QObject(parent)
{
    registerMetaTypes();
}

ServerResult BenchmarkWorker::probeOne(const IngestServer &server)
{
    ServerResult r;
    r.server = server;

    // ── DNS probe ────────────────────────────────────────────────────────────
    auto dnsRes = NetProbe::probeDns(server.host, m_timeoutMs);
    r.dnsSuccess   = dnsRes.success;
    r.dnsLatencyMs = dnsRes.latencyMs;

    if (!dnsRes.success) {
        r.statusNote = "DNS failed";
        return r;
    }

    // ── TCP probes ───────────────────────────────────────────────────────────
    auto tcpResults = NetProbe::probeTcpMulti(server.host, server.port,
                                               m_rounds, m_timeoutMs);
    r.tcpRounds = static_cast<int>(tcpResults.size());

    std::vector<int64_t> good;
    for (const auto &t : tcpResults)
        if (t.success) good.push_back(t.latencyMs);

    r.tcpSuccesses = static_cast<int>(good.size());

    if (!good.empty()) {
        r.tcpMinMs  = *std::min_element(good.begin(), good.end());
        r.tcpMaxMs  = *std::max_element(good.begin(), good.end());
        r.tcpMeanMs = std::accumulate(good.begin(), good.end(), int64_t(0)) /
                      static_cast<int64_t>(good.size());

        // Jitter = sample standard deviation of latencies
        double mean = static_cast<double>(r.tcpMeanMs);
        double variance = 0.0;
        for (auto ms : good)
            variance += (static_cast<double>(ms) - mean) *
                        (static_cast<double>(ms) - mean);
        if (good.size() > 1)
            variance /= static_cast<double>(good.size() - 1);
        r.tcpJitterMs = static_cast<int64_t>(std::sqrt(variance));
    }

    if (r.tcpSuccesses == 0)
        r.statusNote = "All TCP probes failed";
    else if (r.tcpSuccesses < r.tcpRounds)
        r.statusNote = "Partial failures";
    else
        r.statusNote = "OK";

    return r;
}

void BenchmarkWorker::run()
{
    TLOG_INFO("BenchmarkWorker::run() — euOnly=%d rounds=%d timeoutMs=%d",
              m_euOnly, m_rounds, m_timeoutMs);

    emit statusUpdate(QStringLiteral("Fetching Twitch ingest list…"));

    // Fetch ingest list
    auto servers = IngestFetcher::fetch([this](const std::string &msg) {
        emit statusUpdate(QString::fromStdString(msg));
    });

    if (servers.empty()) {
        emit error(QStringLiteral("Failed to fetch ingest list from Twitch"));
        return;
    }

    // Apply EU filter
    if (m_euOnly)
        servers = IngestFetcher::filterEU(servers);

    if (servers.empty()) {
        emit error(QStringLiteral("No servers found after applying filter"));
        return;
    }

    TLOG_INFO("Probing %zu servers (%s)",
              servers.size(), m_euOnly ? "EU only" : "worldwide");

    emit progressUpdate(0, static_cast<int>(servers.size()));

    // Probe each server
    QList<ServerResult> results;
    results.reserve(static_cast<int>(servers.size()));

    for (int i = 0; i < static_cast<int>(servers.size()); ++i) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            emit error(QStringLiteral("Benchmark cancelled"));
            return;
        }

        const auto &srv = servers[static_cast<size_t>(i)];
        emit statusUpdate("Probing " + QString::fromStdString(srv.name) + "...");

        results.push_back(probeOne(srv));
        emit progressUpdate(i + 1, static_cast<int>(servers.size()));
    }

    // Score
    std::vector<ServerResult> vec(results.begin(), results.end());
    ScoreCalculator::compute(vec);
    results = QList<ServerResult>(vec.begin(), vec.end());

    emit statusUpdate(QStringLiteral("Benchmark complete"));
    emit finished(results);
}

// ─────────────────────────────────────────────────────────────────────────────
// BenchmarkEngine
// ─────────────────────────────────────────────────────────────────────────────

BenchmarkEngine::BenchmarkEngine(QObject *parent)
    : QObject(parent)
{
    registerMetaTypes();
}

BenchmarkEngine::~BenchmarkEngine()
{
    cancel();
}

bool BenchmarkEngine::startBenchmark(bool euOnly, int rounds, int timeoutMs)
{
    if (m_running) {
        TLOG_WARN("startBenchmark: already running");
        return false;
    }

    // Clean up previous thread if it finished normally
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }

    m_running = true;

    auto *worker = new BenchmarkWorker();
    worker->setEuOnly(euOnly);
    worker->setProbeRounds(rounds);
    worker->setProbeTimeoutMs(timeoutMs);

    m_thread = new QThread(this);
    worker->moveToThread(m_thread);

    // Wire up signals
    connect(m_thread, &QThread::started,          worker, &BenchmarkWorker::run);
    connect(worker,   &BenchmarkWorker::statusUpdate,
            this,     &BenchmarkEngine::statusUpdate);
    connect(worker,   &BenchmarkWorker::progressUpdate,
            this,     &BenchmarkEngine::progressUpdate);
    connect(worker,   &BenchmarkWorker::finished,
            this,     &BenchmarkEngine::onWorkerFinished);
    connect(worker,   &BenchmarkWorker::error,
            this,     &BenchmarkEngine::onWorkerError);

    // Cleanup: worker deletes itself when thread finishes.
    // Thread is quit+deleted in cancel() or ~BenchmarkEngine().
    connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);

    m_thread->start();
    return true;
}

void BenchmarkEngine::cancel()
{
    if (m_thread) {
        m_running = false;
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait(5000);
        delete m_thread;
        m_thread = nullptr;
    }
}

void BenchmarkEngine::onWorkerFinished(QList<twitch_bench::ServerResult> results)
{
    m_lastResults.assign(results.begin(), results.end());
    m_lastRunTime = std::time(nullptr);
    m_running     = false;
    // Do not delete m_thread here — cancel() / destructor owns the lifetime.
    emit benchmarkFinished(results);
}

void BenchmarkEngine::onWorkerError(const QString &message)
{
    m_running = false;
    // Do not delete m_thread here — cancel() / destructor owns the lifetime.
    emit benchmarkError(message);
}

} // namespace twitch_bench
