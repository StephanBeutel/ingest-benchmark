#pragma once

#include "core/score-calculator.hpp"
#include <QTableWidget>
#include <QList>

namespace twitch_bench {

// ─────────────────────────────────────────────────────────────────────────────
// ResultsTable — QTableWidget specialised to display ServerResult rows.
// ─────────────────────────────────────────────────────────────────────────────

class ResultsTable : public QTableWidget {
    Q_OBJECT

public:
    explicit ResultsTable(QWidget *parent = nullptr);

    // Replace all rows with `results`.  Call from the main thread only.
    void setResults(const QList<ServerResult> &results);

    // Returns the URL template of the currently selected row, or empty string.
    QString selectedServerUrl() const;
    QString selectedServerName() const;

private:
    enum Column {
        COL_NAME     = 0,
        COL_REGION   = 1,
        COL_HOST     = 2,
        COL_DNS      = 3,
        COL_TCP_MEAN = 4,
        COL_JITTER   = 5,
        COL_ERRORS   = 6,
        COL_SCORE    = 7,
        COL_BEST     = 8,
        COLUMN_COUNT = 9
    };

    // Stored parallel to table rows for URL lookup
    QStringList m_urls;
    QStringList m_names;

    void setupHeaders();
    static QString fmtMs(int64_t ms);
};

} // namespace twitch_bench
