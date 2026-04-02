#include "benchmark-dock.hpp"
#include "results-table.hpp"
#include "obs/obs-integration.hpp"
#include "settings/plugin-settings.hpp"
#include "plugin-support.h"

#include <obs-frontend-api.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QCheckBox>
#include <QProgressBar>
#include <QLabel>
#include <QTextEdit>
#include <QSplitter>
#include <QMessageBox>
#include <QDateTime>
#include <QEvent>
#include <QMetaObject>
#include <QWidget>

#include <ctime>

namespace twitch_bench {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

BenchmarkDock::BenchmarkDock(QWidget *parent)
    : QWidget(parent)
    , m_engine(this)
{
    buildUI();

    auto &cfg = PluginSettings::instance();
    m_chkEuOnly->setChecked(cfg.euOnlyFilter());
    m_chkAutoApply->setChecked(cfg.autoBenchmarkBeforeStream());

    connect(&m_engine, &BenchmarkEngine::statusUpdate,
            this,      &BenchmarkDock::onStatusUpdate);
    connect(&m_engine, &BenchmarkEngine::progressUpdate,
            this,      &BenchmarkDock::onProgressUpdate);
    connect(&m_engine, &BenchmarkEngine::benchmarkFinished,
            this,      &BenchmarkDock::onBenchmarkFinished);
    connect(&m_engine, &BenchmarkEngine::benchmarkError,
            this,      &BenchmarkDock::onBenchmarkError);
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown — cancel any running benchmark and join the worker thread.
// Must be called before OBS tears down Qt (OBS_FRONTEND_EVENT_EXIT).
// ─────────────────────────────────────────────────────────────────────────────

void BenchmarkDock::shutdown()
{
    if (m_streamButton) {
        m_streamButton->removeEventFilter(this);
        m_streamButton = nullptr;
    }
    m_engine.cancel();
    TLOG_INFO("BenchmarkDock::shutdown() complete");
}

// ─────────────────────────────────────────────────────────────────────────────
// onStreamingStarting — called via DirectConnection from the
// OBS_FRONTEND_EVENT_STREAMING_STARTING handler.
//
// By the time this runs, OBS has completed SetupStreaming() (encoders
// initialised, OnStreamConfig() already injected the OAuth stream key) but
// has NOT yet called obs_output_start().  We must NOT call
// obs_frontend_streaming_stop() here: doing so leaves the encoder in a
// broken state.
//
// Strategy:
//   • Fresh cache → apply best server immediately before obs_output_start().
//   • Stale/no cache → let this stream start on whatever server OBS has,
//     then run a background benchmark; when it finishes, apply the best
//     server to the live stream (obs_service_update takes effect on the
//     next keyframe reconnect or can be picked up mid-stream by some
//     implementations, but most importantly it's stored for the next start).
// ─────────────────────────────────────────────────────────────────────────────

void BenchmarkDock::onStreamingStarting()
{
    if (!m_chkAutoApply->isChecked())
        return;

    // Suppress re-entry: this is the stream start WE triggered after
    // benchmarking via the button path.
    if (m_benchmarkTriggered) {
        m_benchmarkTriggered = false;
        return;
    }

    if (m_engine.isRunning())
        return;

    auto &cfg = PluginSettings::instance();
    int ttl = cfg.cacheTtlSeconds();
    std::time_t age = std::time(nullptr) - m_engine.lastRunTime();
    bool freshCache = (ttl > 0 && m_engine.lastRunTime() > 0
                       && age <= ttl && !m_lastResults.isEmpty());

    if (freshCache) {
        // Apply immediately — obs_output_start() hasn't been called yet.
        TLOG_INFO("onStreamingStarting: applying cached result (age=%llds)", (long long)age);
        QString url  = bestServerUrl();
        QString name = bestServerName();
        if (!url.isEmpty()) {
            int placeholder = url.indexOf(QStringLiteral("/{stream_key}"));
            if (placeholder == -1) placeholder = url.indexOf(QStringLiteral("{stream_key}"));
            if (placeholder != -1) url = url.left(placeholder);
            applyServer(url, name);
        }
    } else {
        // No fresh cache — stream starts on current server; run benchmark
        // in background and apply the result to the live stream when done.
        TLOG_INFO("onStreamingStarting: no fresh cache (age=%llds ttl=%ds) — running background benchmark",
                  (long long)age, ttl);
        m_pendingApplyLive = true;
        m_lastResults.clear();
        m_btnApply->setEnabled(false);
        m_progress->setValue(0);
        setControlsEnabled(false);
        appendLog(QStringLiteral("Running background benchmark while streaming…"));
        m_engine.startBenchmark(cfg.euOnlyFilter(), cfg.probeRounds(), cfg.probeTimeoutMs());
    }
}

// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Stream button filter
// ─────────────────────────────────────────────────────────────────────────────

void BenchmarkDock::installStreamButtonFilter()
{
    QWidget *mainWin = static_cast<QWidget *>(obs_frontend_get_main_window());
    if (!mainWin) {
        TLOG_WARN("installStreamButtonFilter: could not get main window");
        return;
    }

    // OBS names this button "streamButton" in its UI file.
    m_streamButton = mainWin->findChild<QObject *>(QStringLiteral("streamButton"));
    if (!m_streamButton) {
        TLOG_WARN("installStreamButtonFilter: streamButton not found");
        return;
    }

    m_streamButton->installEventFilter(this);
    TLOG_INFO("installStreamButtonFilter: event filter installed on streamButton");
}

bool BenchmarkDock::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_streamButton)
        return QWidget::eventFilter(watched, event);

    // Only intercept a left-button MouseButtonRelease (the actual "click").
    if (event->type() != QEvent::MouseButtonRelease)
        return false;

    // Only intercept when: auto-apply is on, not already streaming, not already
    // running a benchmark.
    if (!m_chkAutoApply->isChecked())
        return false;

    if (OBSIntegration::isStreaming())
        return false; // user is stopping the stream — let it through

    if (m_engine.isRunning())
        return false; // benchmark already running, let OBS handle the click

    // Eat this click and start the benchmark+stream sequence instead.
    onBenchmarkAndStart();
    return true; // event consumed — OBS's StartStreaming is NOT called
}

// ─────────────────────────────────────────────────────────────────────────────
// UI construction
// ─────────────────────────────────────────────────────────────────────────────

void BenchmarkDock::buildUI()
{
    setMinimumWidth(480);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto *titleLabel = new QLabel(
        QStringLiteral("<b>Twitch Ingest Benchmark</b>"), this);
    titleLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(titleLabel);

    // ── Options ───────────────────────────────────────────────────────────────
    auto *optGroup  = new QGroupBox(QStringLiteral("Options"), this);
    auto *optLayout = new QVBoxLayout(optGroup);

    m_chkEuOnly = new QCheckBox(QStringLiteral("Only test EU servers"), optGroup);
    optLayout->addWidget(m_chkEuOnly);

    m_chkAutoApply = new QCheckBox(
        QStringLiteral("Auto-benchmark and apply best server when starting stream"),
        optGroup);
    optLayout->addWidget(m_chkAutoApply);

    root->addWidget(optGroup);

    connect(m_chkEuOnly,    &QCheckBox::toggled, this, &BenchmarkDock::onEuOnlyToggled);
    connect(m_chkAutoApply, &QCheckBox::toggled, this, &BenchmarkDock::onAutoApplyToggled);

    // ── Buttons ───────────────────────────────────────────────────────────────
    auto *btnLayout = new QHBoxLayout();
    m_btnRun           = new QPushButton(QStringLiteral("Run Benchmark"),       this);
    m_btnApply         = new QPushButton(QStringLiteral("Apply Best Server"),   this);
    m_btnBenchAndStart = new QPushButton(QStringLiteral("Benchmark && Stream"), this);
    m_btnApply->setEnabled(false);
    btnLayout->addWidget(m_btnRun);
    btnLayout->addWidget(m_btnApply);
    btnLayout->addWidget(m_btnBenchAndStart);
    root->addLayout(btnLayout);

    connect(m_btnRun,           &QPushButton::clicked, this, &BenchmarkDock::onRunBenchmark);
    connect(m_btnApply,         &QPushButton::clicked, this, &BenchmarkDock::onApplyBestServer);
    connect(m_btnBenchAndStart, &QPushButton::clicked, this, &BenchmarkDock::onBenchmarkAndStart);

    // ── Progress ──────────────────────────────────────────────────────────────
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    root->addWidget(m_progress);

    m_statusLabel = new QLabel(QStringLiteral("Ready"), this);
    m_statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    root->addWidget(m_statusLabel);

    // ── Log + table ───────────────────────────────────────────────────────────
    auto *splitter = new QSplitter(Qt::Vertical, this);

    m_log = new QTextEdit(splitter);
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(80);
    m_log->setPlaceholderText(QStringLiteral("Benchmark log…"));
    m_log->document()->setMaximumBlockCount(200);

    m_table = new ResultsTable(splitter);

    splitter->addWidget(m_log);
    splitter->addWidget(m_table);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    root->addWidget(splitter, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

void BenchmarkDock::setControlsEnabled(bool enabled)
{
    m_btnRun->setEnabled(enabled);
    m_btnApply->setEnabled(enabled && !m_lastResults.isEmpty());
    m_btnBenchAndStart->setEnabled(enabled);
    m_chkEuOnly->setEnabled(enabled);
    m_chkAutoApply->setEnabled(enabled);
}

void BenchmarkDock::appendLog(const QString &msg)
{
    QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_log->append("[" + ts + "] " + msg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Button handlers
// ─────────────────────────────────────────────────────────────────────────────

void BenchmarkDock::onRunBenchmark()
{
    if (m_engine.isRunning()) return;
    m_pendingStreamStart = false;

    m_lastResults.clear();
    m_btnApply->setEnabled(false);
    m_progress->setValue(0);
    setControlsEnabled(false);
    appendLog(QStringLiteral("Starting benchmark…"));

    auto &cfg = PluginSettings::instance();
    m_engine.startBenchmark(cfg.euOnlyFilter(),
                            cfg.probeRounds(),
                            cfg.probeTimeoutMs());
}

void BenchmarkDock::onApplyBestServer()
{
    QString url  = bestServerUrl();
    QString name = bestServerName();
    if (url.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("No Results"),
            QStringLiteral("Run a benchmark first, or select a row in the table."));
        return;
    }
    applyServer(url, name);
}

void BenchmarkDock::onBenchmarkAndStart()
{
    if (m_engine.isRunning()) return;
    if (OBSIntegration::isStreaming()) {
        QMessageBox::information(this, QStringLiteral("Already Streaming"),
            QStringLiteral("OBS is already streaming."));
        return;
    }
    startBenchmarkForStream();
}

void BenchmarkDock::startBenchmarkForStream()
{
    if (m_engine.isRunning()) return;

    m_pendingStreamStart = true;
    m_lastResults.clear();
    m_btnApply->setEnabled(false);
    m_progress->setValue(0);
    setControlsEnabled(false);
    appendLog(QStringLiteral("Benchmarking before stream start…"));

    auto &cfg = PluginSettings::instance();
    m_engine.startBenchmark(cfg.euOnlyFilter(),
                            cfg.probeRounds(),
                            cfg.probeTimeoutMs());
}

// ─────────────────────────────────────────────────────────────────────────────
// Apply a server URL to OBS
// ─────────────────────────────────────────────────────────────────────────────

void BenchmarkDock::applyServer(const QString &url, const QString &name)
{
    QString cleanUrl = url;
    int placeholder = cleanUrl.indexOf(QStringLiteral("/{stream_key}"));
    if (placeholder == -1)
        placeholder = cleanUrl.indexOf(QStringLiteral("{stream_key}"));
    if (placeholder != -1)
        cleanUrl = cleanUrl.left(placeholder);

    bool ok = OBSIntegration::applyStreamServer(std::string(cleanUrl.toUtf8().constData()));
    if (ok) {
        appendLog("Applied server: " + name + " -> " + cleanUrl);
        m_statusLabel->setText("Server applied: " + name);
    } else {
        appendLog(QStringLiteral("ERROR: failed to apply server"));
        QMessageBox::critical(this, QStringLiteral("Error"),
            QStringLiteral("Could not update OBS stream settings.\n"
                           "Make sure a streaming service is configured in OBS."));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Best server lookup
// ─────────────────────────────────────────────────────────────────────────────

QString BenchmarkDock::bestServerUrl() const
{
    QString sel = m_table->selectedServerUrl();
    if (!sel.isEmpty()) return sel;
    for (const auto &r : m_lastResults)
        if (r.recommended)
            return QString::fromStdString(r.server.urlTemplate);
    return {};
}

QString BenchmarkDock::bestServerName() const
{
    QString sel = m_table->selectedServerName();
    if (!sel.isEmpty()) return sel;
    for (const auto &r : m_lastResults)
        if (r.recommended)
            return QString::fromStdString(r.server.name);
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Engine signal handlers
// ─────────────────────────────────────────────────────────────────────────────

void BenchmarkDock::onStatusUpdate(const QString &msg)
{
    m_statusLabel->setText(msg);
    appendLog(msg);
}

void BenchmarkDock::onProgressUpdate(int current, int total)
{
    if (total > 0) {
        m_progress->setRange(0, total);
        m_progress->setValue(current);
        m_progress->setFormat(
            QString::number(current) + " / " + QString::number(total));
    }
}

void BenchmarkDock::onBenchmarkFinished(QList<twitch_bench::ServerResult> results)
{
    m_lastResults = results;
    m_table->setResults(results);
    setControlsEnabled(true);
    m_progress->setValue(m_progress->maximum());
    appendLog("Benchmark complete — " + QString::number(results.size()) + " servers tested");

    if (m_pendingStreamStart) {
        m_pendingStreamStart = false;

        QString url  = bestServerUrl();
        QString name = bestServerName();
        if (!url.isEmpty())
            applyServer(url, name);

        // Queue the stream start so we return to the event loop first.
        // Set the flag so onStreamingStarting() knows not to intercept this.
        appendLog(QStringLiteral("Starting stream…"));
        m_benchmarkTriggered = true;
        QMetaObject::invokeMethod(this, []() {
            obs_frontend_streaming_start();
        }, Qt::QueuedConnection);
    }

    if (m_pendingApplyLive) {
        m_pendingApplyLive = false;

        QString url  = bestServerUrl();
        QString name = bestServerName();
        if (!url.isEmpty()) {
            applyServer(url, name);
            appendLog(QStringLiteral("Best server applied to live stream — will take effect on next stream start."));
        }
    }
}

void BenchmarkDock::onBenchmarkError(const QString &message)
{
    m_pendingStreamStart = false;
    m_pendingApplyLive   = false;
    setControlsEnabled(true);
    appendLog("ERROR: " + message);
    m_statusLabel->setText("Error: " + message);
    TLOG_ERROR("Benchmark error: %s", message.toUtf8().constData());
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings toggles
// ─────────────────────────────────────────────────────────────────────────────

void BenchmarkDock::onEuOnlyToggled(bool checked)
{
    PluginSettings::instance().setEuOnlyFilter(checked);
}

void BenchmarkDock::onAutoApplyToggled(bool checked)
{
    PluginSettings::instance().setAutoBenchmarkBeforeStream(checked);
}

} // namespace twitch_bench
