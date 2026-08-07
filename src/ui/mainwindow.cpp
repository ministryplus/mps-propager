#include "mainwindow.h"

#include <QCloseEvent>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(PagerController *pager, QWidget *parent)
    : QMainWindow(parent), m_pager(pager)
{
    setWindowTitle(QStringLiteral("ProPager")); // was "Village Kids Pager"
    resize(300, 200);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    m_activeLabel = new QLabel(QStringLiteral("Active Number: N/A"), central);
    m_activeLabel->setAlignment(Qt::AlignLeading);
    layout->addWidget(m_activeLabel);

    m_queueLabel = new QLabel(QStringLiteral("Numbers Queued:"), central);
    m_queueLabel->setAlignment(Qt::AlignLeading);
    layout->addWidget(m_queueLabel);

    layout->addStretch();
    setCentralWidget(central);

    // Two permanent status-bar widgets (ports Status in overview.py).
    m_slackStatus = new QLabel(QStringLiteral("Slack: Disconnected"), this);
    m_proPresStatus = new QLabel(QStringLiteral("ProPres: Disconnected"), this);
    statusBar()->addPermanentWidget(m_slackStatus);
    statusBar()->addPermanentWidget(m_proPresStatus);
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

void MainWindow::showSetupError(const QString &message)
{
    QMessageBox box(QMessageBox::Critical, QStringLiteral("Error!"), message,
                    QMessageBox::Ok, this);
    box.exec();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Keep the app alive in the tray; only the Quit action exits.
    event->ignore();
    hide();
}
