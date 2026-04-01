#pragma once

#include "score-calculator.hpp"
#include <QObject>
#include <QThread>
#include <vector>
#include <atomic>
#include <ctime>

namespace twitch_bench {

// ─────────────────────────────────────────────────────────────────────────────
// BenchmarkWorker — runs inside a QThread.
// Emits signals back to the main thread with progress and results.
// ─────────────────────────────────────────────────────────────────────────────

class BenchmarkWorker : public QObject {
    Q_OBJECT

public:
    explicit BenchmarkWorker(QObject *parent = nullptr);

    void setEuOnly(bool v)         { m_euOnly = v; }
    void setProbeRounds(int v)     { m_rounds = v; }
    void setProbeTimeoutMs(int v)  { m_timeoutMs = v; }

public slots:
    void run();    // Called by QThread::started()

signals:
    void statusUpdate(const QString &message);
    void progressUpdate(int current, int total);
    void finished(QList<twitch_bench::ServerResult> results);
    void error(const QString &message);

private:
    bool m_euOnly     = false;
    int  m_rounds     = 3;
    int  m_timeoutMs  = 3000;
    std::atomic<bool> m_abort{false};

    ServerResult probeOne(const IngestServer &server);
};

// ─────────────────────────────────────────────────────────────────────────────
// BenchmarkEngine — manages the worker thread lifecycle.
// Created once per dock; safe to run multiple benchmarks sequentially.
// ─────────────────────────────────────────────────────────────────────────────

class BenchmarkEngine : public QObject {
    Q_OBJECT

public:
    explicit BenchmarkEngine(QObject *parent = nullptr);
    ~BenchmarkEngine();

    bool isRunning() const { return m_running; }

    // Call from main thread.  Returns false if already running.
    bool startBenchmark(bool euOnly, int rounds, int timeoutMs);

    // Request cancellation (non-blocking — signals when done)
    void cancel();

    // Access last results (only valid after benchmarkFinished)
    const std::vector<ServerResult> &lastResults() const { return m_lastResults; }
    std::time_t lastRunTime() const { return m_lastRunTime; }

signals:
    void statusUpdate(const QString &message);
    void progressUpdate(int current, int total);
    void benchmarkFinished(QList<twitch_bench::ServerResult> results);
    void benchmarkError(const QString &message);

private slots:
    void onWorkerFinished(QList<twitch_bench::ServerResult> results);
    void onWorkerError(const QString &message);

private:
    QThread *m_thread = nullptr;
    bool     m_running = false;

    std::vector<ServerResult> m_lastResults;
    std::time_t               m_lastRunTime = 0;
};

} // namespace twitch_bench

// Make ServerResult usable in Qt queued connections
Q_DECLARE_METATYPE(twitch_bench::ServerResult)
Q_DECLARE_METATYPE(QList<twitch_bench::ServerResult>)
