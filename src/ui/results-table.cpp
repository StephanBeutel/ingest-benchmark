#include "results-table.hpp"

#include <QHeaderView>
#include <QColor>
#include <QFont>
#include <QTableWidgetItem>

namespace twitch_bench {

ResultsTable::ResultsTable(QWidget *parent)
    : QTableWidget(0, COLUMN_COUNT, parent)
{
    setupHeaders();

    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setAlternatingRowColors(true);
    setShowGrid(true);
    setSortingEnabled(true);

    horizontalHeader()->setSectionResizeMode(COL_NAME,     QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(COL_HOST,     QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(COL_REGION,   QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(COL_DNS,      QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(COL_TCP_MEAN, QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(COL_JITTER,   QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(COL_ERRORS,   QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(COL_SCORE,    QHeaderView::ResizeToContents);
    horizontalHeader()->setSectionResizeMode(COL_BEST,     QHeaderView::ResizeToContents);

    verticalHeader()->setVisible(false);
}

void ResultsTable::setupHeaders()
{
    setHorizontalHeaderLabels({
        QStringLiteral("Name"),
        QStringLiteral("Region"),
        QStringLiteral("Host"),
        QStringLiteral("DNS (ms)"),
        QStringLiteral("TCP (ms)"),
        QStringLiteral("Jitter (ms)"),
        QStringLiteral("Errors"),
        QStringLiteral("Score"),
        QStringLiteral("Best?")
    });
}

QString ResultsTable::fmtMs(int64_t ms)
{
    if (ms < 0) return QStringLiteral("–");
    return QString::number(ms);
}

void ResultsTable::setResults(const QList<ServerResult> &results)
{
    setSortingEnabled(false);
    setRowCount(0);
    m_urls.clear();
    m_names.clear();

    setRowCount(results.size());

    for (int row = 0; row < results.size(); ++row) {
        const ServerResult &r = results[row];

        int errors = r.tcpRounds - r.tcpSuccesses;

        auto *nameItem   = new QTableWidgetItem(QString::fromStdString(r.server.name));
        auto *regionItem = new QTableWidgetItem(QString::fromStdString(r.server.region));
        auto *hostItem   = new QTableWidgetItem(QString::fromStdString(r.server.host));
        auto *dnsItem    = new QTableWidgetItem(fmtMs(r.dnsLatencyMs));
        auto *tcpItem    = new QTableWidgetItem(fmtMs(r.tcpMeanMs));
        auto *jitterItem = new QTableWidgetItem(fmtMs(r.tcpJitterMs));
        auto *errItem    = new QTableWidgetItem(QString::number(errors));
        auto *scoreItem  = new QTableWidgetItem(
            r.score > 0 ? QString::number(r.score, 'f', 1)
                        : QStringLiteral("–"));
        auto *bestItem   = new QTableWidgetItem(r.recommended ? QStringLiteral("★") : QString());

        // Numeric sort roles for sortable columns
        dnsItem->setData(Qt::UserRole,    (qlonglong)r.dnsLatencyMs);
        tcpItem->setData(Qt::UserRole,    (qlonglong)r.tcpMeanMs);
        jitterItem->setData(Qt::UserRole, (qlonglong)r.tcpJitterMs);
        errItem->setData(Qt::UserRole,    errors);
        scoreItem->setData(Qt::UserRole,  r.score);

        // Align numeric columns to right
        for (auto *item : {dnsItem, tcpItem, jitterItem, errItem, scoreItem})
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bestItem->setTextAlignment(Qt::AlignCenter);

        // Highlight recommended server
        if (r.recommended) {
            QFont boldFont = nameItem->font();
            boldFont.setBold(true);
            for (int c = 0; c < COLUMN_COUNT; ++c) {
                // We set it per-item below
            }
            auto setHighlight = [&](QTableWidgetItem *item) {
                item->setBackground(QColor(40, 120, 40, 80));
                item->setFont(boldFont);
            };
            setHighlight(nameItem);
            setHighlight(regionItem);
            setHighlight(hostItem);
            setHighlight(dnsItem);
            setHighlight(tcpItem);
            setHighlight(jitterItem);
            setHighlight(errItem);
            setHighlight(scoreItem);
            setHighlight(bestItem);
        }

        // Warn: high error count
        if (errors > 0 && r.tcpRounds > 0) {
            errItem->setForeground(errors == r.tcpRounds
                ? QColor(220, 50, 50)
                : QColor(200, 150, 0));
        }

        setItem(row, COL_NAME,     nameItem);
        setItem(row, COL_REGION,   regionItem);
        setItem(row, COL_HOST,     hostItem);
        setItem(row, COL_DNS,      dnsItem);
        setItem(row, COL_TCP_MEAN, tcpItem);
        setItem(row, COL_JITTER,   jitterItem);
        setItem(row, COL_ERRORS,   errItem);
        setItem(row, COL_SCORE,    scoreItem);
        setItem(row, COL_BEST,     bestItem);

        m_urls.append(QString::fromStdString(r.server.urlTemplate));
        m_names.append(QString::fromStdString(r.server.name));
    }

    setSortingEnabled(true);
    // Default sort: score descending
    sortByColumn(COL_SCORE, Qt::DescendingOrder);
}

QString ResultsTable::selectedServerUrl() const
{
    int row = currentRow();
    if (row < 0 || row >= m_urls.size()) return {};
    return m_urls.at(row);
}

QString ResultsTable::selectedServerName() const
{
    int row = currentRow();
    if (row < 0 || row >= m_names.size()) return {};
    return m_names.at(row);
}

} // namespace twitch_bench
