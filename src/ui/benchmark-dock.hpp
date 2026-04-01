#pragma once

#include "core/benchmark-engine.hpp"
#include <QWidget>
#include <QList>

// Forward declarations
class QCheckBox;
class QPushButton;
class QProgressBar;
class QLabel;
class QTextEdit;
class QSplitter;

namespace twitch_bench {

class ResultsTable;

// ─────────────────────────────────────────────────────────────────────────────
// BenchmarkDock — the top-level QWidget added to OBS as a dock.
// ─────────────────────────────────────────────────────────────────────────────

class BenchmarkDock : public QWidget {
    Q_OBJECT

public:
    explicit BenchmarkDock(QWidget *parent = nullptr);
    ~BenchmarkDock() override = default;

    // Install an event filter on OBS's stream button so we can intercept
    // clicks when auto-benchmark is enabled. Called once after the dock
    // is registered and OBS's main window is fully initialised.
    void installStreamButtonFilter();

    // QObject event filter — intercepts stream button clicks.
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onRunBenchmark();
    void onApplyBestServer();
    void onBenchmarkAndStart();

    void onStatusUpdate(const QString &msg);
    void onProgressUpdate(int current, int total);
    void onBenchmarkFinished(QList<twitch_bench::ServerResult> results);
    void onBenchmarkError(const QString &msg);

    void onEuOnlyToggled(bool checked);
    void onAutoApplyToggled(bool checked);

private:
    void buildUI();
    void setControlsEnabled(bool enabled);
    void appendLog(const QString &msg);
    void applyServer(const QString &url, const QString &name);

    QString bestServerUrl() const;
    QString bestServerName() const;

    // ── Widgets ──────────────────────────────────────────────────────────────
    QPushButton  *m_btnRun           = nullptr;
    QPushButton  *m_btnApply         = nullptr;
    QPushButton  *m_btnBenchAndStart = nullptr;
    QCheckBox    *m_chkEuOnly        = nullptr;
    QCheckBox    *m_chkAutoApply     = nullptr;
    QProgressBar *m_progress         = nullptr;
    QLabel       *m_statusLabel      = nullptr;
    QTextEdit    *m_log              = nullptr;
    ResultsTable *m_table            = nullptr;

    // ── State ─────────────────────────────────────────────────────────────────
    BenchmarkEngine         m_engine;
    QList<ServerResult>     m_lastResults;
    bool                    m_pendingStreamStart = false;
    QObject                *m_streamButton       = nullptr; // OBS main window stream button
};

} // namespace twitch_bench
