#include "mainwindow.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include "connectionstab.h"

namespace {
// Cap the in-window log so a long service cannot grow it without bound; the full
// history still lives in the on-disk ProPager.log.
constexpr int kMaxLogBlocks = 5000;
} // namespace

MainWindow::MainWindow(PagerController *pager, Config *config,
                       const QString &logDir, QWidget *parent)
    : QMainWindow(parent), m_pager(pager), m_config(config), m_logDir(logDir)
{
    setWindowTitle(QStringLiteral("ProPager")); // was "Village Kids Pager"
    resize(360, 320);

    // The central widget stacks a validation banner (Decision 8) above the
    // tabs, so config problems are visible even when the Connections tab is not
    // the front tab. The banner starts hidden and is driven by showValidation().
    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_banner = new QFrame(central);
    m_banner->setFrameShape(QFrame::StyledPanel);
    auto *bannerLayout = new QVBoxLayout(m_banner);
    m_bannerLabel = new QLabel(m_banner);
    m_bannerLabel->setWordWrap(true);
    bannerLayout->addWidget(m_bannerLabel);
    m_banner->hide();
    rootLayout->addWidget(m_banner);

    auto *tabs = new QTabWidget(central);
    // Order: Overview / Connections / Log (Decision 1).
    tabs->addTab(buildOverviewTab(), QStringLiteral("Overview"));
    tabs->addTab(buildConnectionsTab(), QStringLiteral("Connections"));
    tabs->addTab(buildLogTab(), QStringLiteral("Log"));
    rootLayout->addWidget(tabs);

    setCentralWidget(central);

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

QWidget *MainWindow::buildConnectionsTab()
{
    // The tab body (per-connection forms, dirty-tracking, inline validation, the
    // path-labeled Reveal Config File) is Task 002-4; MainWindow only mounts it
    // and keeps a handle so main.cpp (002-7) can reach its signals.
    m_connectionsTab = new ConnectionsTab(*m_config, this);
    return m_connectionsTab;
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
    // Window-level Reveal Config File (Decision 13), reachable before the
    // Connections tab is opened. The .ini and ProPager.log share m_logDir
    // (Config::configDir()), so this opens the same directory as Reveal Log.
    // NOTE: the Connections tab (Task 002-4) owns the *path-labeled* Reveal
    // Config File; this is the deliberate second, plainer affordance — the two
    // do not duplicate each other.
    auto *revealConfig =
        new QPushButton(QStringLiteral("Reveal Config File"), tab);
    connect(revealConfig, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_logDir));
    });
    auto *clear = new QPushButton(QStringLiteral("Clear"), tab);
    connect(clear, &QPushButton::clicked, m_logView, &QPlainTextEdit::clear);

    buttons->addWidget(reveal);
    buttons->addWidget(revealConfig);
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

MainWindow::BannerContent
MainWindow::bannerContent(const Config::ValidationResult &result)
{
    // Dismissible-on-fix: a clean result hides the banner entirely.
    if (result.isClean())
        return {BannerTier::None, QString()};

    // Required-missing takes visual precedence over shape-warnings: when the
    // config can't connect at all, that is the only thing the operator needs to
    // see (Decision 6/8). Each entry carries its own human-readable message.
    if (result.hasBlockingErrors()) {
        QStringList lines;
        lines.reserve(result.requiredMissing.size());
        for (const Config::ValidationEntry &e : result.requiredMissing)
            lines << e.message;
        return {BannerTier::Error,
                QStringLiteral("Fix these to connect:\n") +
                    lines.join(QLatin1Char('\n'))};
    }

    // Shape-only warnings are informational — connection still proceeds.
    QStringList lines;
    lines.reserve(result.shapeWarnings.size());
    for (const Config::ValidationEntry &e : result.shapeWarnings)
        lines << e.message;
    return {BannerTier::Warning,
            QStringLiteral("These settings look off (still connecting):\n") +
                lines.join(QLatin1Char('\n'))};
}

void MainWindow::showValidation(const Config::ValidationResult &result)
{
    const BannerContent content = bannerContent(result);
    if (content.tier == BannerTier::None) {
        m_banner->hide();
        return;
    }

    m_bannerLabel->setText(content.message);
    // Red = must fix, amber = looks off, so the tier reads at a glance.
    const QString style =
        content.tier == BannerTier::Error
            ? QStringLiteral("QFrame { background: #c0392b; } "
                             "QLabel { color: white; }")
            : QStringLiteral("QFrame { background: #f39c12; } "
                             "QLabel { color: black; }");
    m_banner->setStyleSheet(style);
    m_banner->show();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Keep the app alive in the tray; only the Quit action exits.
    event->ignore();
    hide();
}
