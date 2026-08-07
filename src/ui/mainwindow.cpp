#include "mainwindow.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace {
// Cap the in-window log so a long service cannot grow it without bound; the full
// history still lives in the on-disk ProPager.log.
constexpr int kMaxLogBlocks = 5000;
} // namespace

MainWindow::MainWindow(PagerController *pager, const QString &logDir,
                       QWidget *parent)
    : QMainWindow(parent), m_pager(pager), m_logDir(logDir)
{
    setWindowTitle(QStringLiteral("ProPager")); // was "Village Kids Pager"
    resize(360, 260);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildOverviewTab(), QStringLiteral("Overview"));
    tabs->addTab(buildLogTab(), QStringLiteral("Log"));
    setCentralWidget(tabs);

    // Two permanent status-bar widgets (ports Status in overview.py).
    m_slackStatus = new QLabel(QStringLiteral("Slack: Disconnected"), this);
    m_proPresStatus = new QLabel(QStringLiteral("ProPres: Disconnected"), this);
    statusBar()->addPermanentWidget(m_slackStatus);
    statusBar()->addPermanentWidget(m_proPresStatus);
}

QWidget *MainWindow::buildOverviewTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);

    m_activeLabel = new QLabel(QStringLiteral("Active Number: N/A"), tab);
    m_activeLabel->setAlignment(Qt::AlignLeading);
    layout->addWidget(m_activeLabel);

    m_queueLabel = new QLabel(QStringLiteral("Numbers Queued:"), tab);
    m_queueLabel->setAlignment(Qt::AlignLeading);
    layout->addWidget(m_queueLabel);

    layout->addStretch();
    return tab;
}

QWidget *MainWindow::buildLogTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);

    m_logView = new QPlainTextEdit(tab);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(kMaxLogBlocks);
    m_logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(m_logView);

    auto *buttons = new QHBoxLayout;
    auto *reveal = new QPushButton(QStringLiteral("Reveal Log File"), tab);
    connect(reveal, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_logDir));
    });
    auto *clear = new QPushButton(QStringLiteral("Clear"), tab);
    connect(clear, &QPushButton::clicked, m_logView, &QPlainTextEdit::clear);

    buttons->addWidget(reveal);
    buttons->addWidget(clear);
    buttons->addStretch();
    layout->addLayout(buttons);

    return tab;
}

QString MainWindow::formatActiveLine(const QString &lastNumber,
                                     const QString &activeFormatted)
{
    QString text;
    if (!lastNumber.isEmpty())
        text = QStringLiteral("Last Number: %1\n").arg(lastNumber);

    if (!activeFormatted.isEmpty()) {
        // A multi-number batch is the only thing formatBatch joins with " & ".
        const QString plural =
            activeFormatted.contains(QStringLiteral(" & ")) ? QStringLiteral("s")
                                                            : QString();
        text += QStringLiteral("Active Number%1: %2").arg(plural, activeFormatted);
    } else {
        text += QStringLiteral("Active Number: N/A");
    }
    return text.trimmed(); // ports overview.py's .strip()
}

QString MainWindow::formatQueue(const QList<PagerController::Batch> &batches)
{
    if (batches.isEmpty())
        return QStringLiteral("Numbers Queued:");

    QStringList lines;
    lines.reserve(batches.size());
    for (const PagerController::Batch &batch : batches)
        lines << PagerController::formatBatch(batch);

    return QStringLiteral("Numbers Queued:\n") + lines.join(QLatin1Char('\n'));
}

void MainWindow::setSlackConnected(bool connected)
{
    m_slackStatus->setText(connected ? QStringLiteral("Slack: Connected")
                                     : QStringLiteral("Slack: Disconnected"));
}

void MainWindow::setProPresConnected(bool connected)
{
    m_proPresStatus->setText(connected ? QStringLiteral("ProPres: Connected")
                                       : QStringLiteral("ProPres: Disconnected"));
}

void MainWindow::refresh()
{
    if (!m_pager)
        return;

    m_activeLabel->setText(
        formatActiveLine(m_pager->lastNumber(), m_pager->activeFormatted()));

    // Ports overview.py: the ready batches plus the still-forming batch.
    QList<PagerController::Batch> batches = m_pager->queuedBatches();
    const PagerController::Batch forming = m_pager->currentBatch();
    if (!forming.isEmpty())
        batches.append(forming);

    m_queueLabel->setText(formatQueue(batches));
}

void MainWindow::appendLog(const QString &line)
{
    // appendPlainText keeps the view scrolled to the newest entry.
    m_logView->appendPlainText(line);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Keep the app alive in the tray; only the Quit action exits.
    event->ignore();
    hide();
}
