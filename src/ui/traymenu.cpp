#include "traymenu.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QUrl>

#include "pagercontroller.h"

namespace {

// Derive the warning variant from the normal glyph (Task 002-6, step 6):
// recolour its silhouette amber so the delta reads at a glance in the menu bar,
// while the tooltip carries the actual reason. A concrete colour — not a mask —
// so it stays visible in both light and dark bars. Covers the 1x and 2x
// (~18pt-tall) menu-bar slots so Retina stays crisp.
QIcon tintedWarningIcon(const QIcon &glyph)
{
    const QColor amber(0xE6, 0x9A, 0x00);
    QIcon warn;
    for (int edge : {18, 36}) {
        QPixmap base = glyph.pixmap(QSize(edge, edge));
        if (base.isNull())
            continue;
        QPixmap tinted(base.size());
        tinted.setDevicePixelRatio(base.devicePixelRatio());
        tinted.fill(Qt::transparent);
        QPainter p(&tinted);
        p.drawPixmap(0, 0, base);
        // Keep the glyph's alpha silhouette, replace its colour with amber.
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(tinted.rect(), amber);
        p.end();
        warn.addPixmap(tinted);
    }
    return warn;
}

} // namespace

TrayMenu::TrayMenu(PagerController *pager, QObject *parent)
    : QObject(parent), m_pager(pager)
{
    m_menu = new QMenu;
    m_tray = new QSystemTrayIcon(this);

    // Menu-bar assets (Task 8). The glyph ships as a Qt resource; setIsMask makes
    // macOS render it as a template image (black in a light bar, white in a dark
    // one), matching the *Template naming convention. The warning variant is
    // derived from it. If the resource is ever missing (e.g. a stripped test
    // build) fall back to the Task 1 standard icons so the tray still renders.
    QIcon glyph(QStringLiteral(":/icons/MenubarTemplate.png"));
    if (glyph.isNull()) {
        m_normalIcon = qApp->style()->standardIcon(QStyle::SP_ComputerIcon);
        m_warningIcon = qApp->style()->standardIcon(QStyle::SP_MessageBoxWarning);
    } else {
        glyph.setIsMask(true);
        m_normalIcon = glyph;
        m_warningIcon = tintedWarningIcon(glyph);
    }
    m_tray->setIcon(m_normalIcon);
    m_tray->setToolTip(QStringLiteral("ProPager"));

    // Order mirrors widget.py: ProPresenter status, Slack status, separator,
    // active number, separator, Open Window, Quit.
    m_proPresStatus = m_menu->addAction(QStringLiteral("ProPres: Disconnected"));
    m_proPresStatus->setEnabled(false);

    m_slackStatus = m_menu->addAction(QStringLiteral("Slack: Disconnected"));
    m_slackStatus->setEnabled(false);

    m_menu->addSeparator();

    m_activeAction = m_menu->addAction(QStringLiteral("Active Number: N/A"));
    m_activeAction->setEnabled(false);

    m_menu->addSeparator();

    // Single coarse Reconnect (Decision 12) — no per-connection items; that
    // lives on the Connections tab. Sits with the actionable items above Quit.
    m_reconnectAction = m_menu->addAction(QStringLiteral("Reconnect"));
    connect(m_reconnectAction, &QAction::triggered, this,
            &TrayMenu::reconnectRequested);

    QAction *openAction = m_menu->addAction(QStringLiteral("Open Window"));
    connect(openAction, &QAction::triggered, this,
            &TrayMenu::openWindowRequested);

    // About carries the Qt/LGPLv3 attribution and the route to the bundled
    // license texts (LGPL compliance) — see showAbout().
    QAction *aboutAction = m_menu->addAction(QStringLiteral("About ProPager"));
    connect(aboutAction, &QAction::triggered, this, &TrayMenu::showAbout);

    QAction *quitAction = m_menu->addAction(QStringLiteral("Quit"));
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    m_tray->setContextMenu(m_menu);
    m_tray->setVisible(true);

    // Both clients start disconnected, so render the initial warning state.
    refreshWarningState();
}

void TrayMenu::showAbout()
{
    // Where the license texts live in a released bundle: MacOS/ (applicationDir)
    // has a sibling Resources/licenses/ — mirrors the CMake install DESTINATION.
    // In a plain dev/test build this directory won't exist; we fall back to the
    // online Qt licensing page so the button always leads somewhere useful.
    const QString licensesDir = QDir::cleanPath(
        QCoreApplication::applicationDirPath() +
        QStringLiteral("/../Resources/licenses"));
    const bool haveBundledLicenses = QDir(licensesDir).exists();

    const QString version = QCoreApplication::applicationVersion();
    const QString heading = version.isEmpty()
                                ? QStringLiteral("ProPager")
                                : QStringLiteral("ProPager %1").arg(version);

    // Rich text so the Qt source / licensing links are clickable. The Qt version
    // is the compile-time QT_VERSION_STR, so it always matches what we linked.
    const QString body = QStringLiteral(
        "<p><b>%1</b><br>Copyright &copy; 2026 Ministry Plus Solutions Inc.<br>"
        "Released under the MIT License.</p>"
        "<p>This application uses the <b>Qt framework %2</b> under the GNU Lesser "
        "General Public License v3.0 (LGPLv3), which incorporates the GNU GPL "
        "v3.0. Qt is dynamically linked and may be replaced with a compatible "
        "modified build.</p>"
        "<p>Corresponding Qt source: "
        "<a href=\"https://download.qt.io/archive/qt/6.8/6.8.3/single/\">"
        "download.qt.io</a> &middot; "
        "<a href=\"https://www.qt.io/licensing/\">Qt licensing</a></p>")
        .arg(heading, QStringLiteral(QT_VERSION_STR));

    QMessageBox box;
    box.setWindowTitle(QStringLiteral("About ProPager"));
    box.setTextFormat(Qt::RichText);
    box.setText(body);
    box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    box.setIconPixmap(m_normalIcon.pixmap(QSize(64, 64)));

    QPushButton *showLicenses =
        box.addButton(QStringLiteral("Show Licenses"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.setDefaultButton(QMessageBox::Close);
    box.exec();

    if (box.clickedButton() == showLicenses) {
        // Reveal the bundled license folder, or the online licensing page when
        // running outside a deployed bundle.
        QDesktopServices::openUrl(
            haveBundledLicenses
                ? QUrl::fromLocalFile(licensesDir)
                : QUrl(QStringLiteral("https://www.qt.io/licensing/")));
    }
}

QString TrayMenu::warningTooltip(bool configValid, const QString &configSummary,
                                 bool slackConnected, bool proPresConnected)
{
    // Precedence (a): a required key is unset → cannot connect. Most actionable,
    // so it wins even when a client is also down.
    if (!configValid) {
        return configSummary.isEmpty()
                   ? QStringLiteral("Configuration incomplete")
                   : configSummary;
    }

    // Precedence (b): config is fine, so report which client is down.
    if (!slackConnected && !proPresConnected)
        return QStringLiteral("Slack and ProPresenter disconnected");
    if (!slackConnected)
        return QStringLiteral("Slack disconnected");
    if (!proPresConnected)
        return QStringLiteral("ProPresenter disconnected");

    // Config valid and both clients up → normal state.
    return QString();
}

void TrayMenu::setSlackConnected(bool connected)
{
    m_slackConnected = connected;
    m_slackStatus->setText(connected ? QStringLiteral("Slack: Connected")
                                     : QStringLiteral("Slack: Disconnected"));
    refreshWarningState();
}

void TrayMenu::setProPresConnected(bool connected)
{
    m_proPresConnected = connected;
    m_proPresStatus->setText(connected ? QStringLiteral("ProPres: Connected")
                                       : QStringLiteral("ProPres: Disconnected"));
    refreshWarningState();
}

void TrayMenu::setConfigValidation(const Config::ValidationResult &result)
{
    // Only required-but-unset keys block connecting (Decision 6); shape warnings
    // are surfaced on the tab/banner, not the tray.
    m_configValid = !result.hasBlockingErrors();

    QStringList reasons;
    reasons.reserve(result.requiredMissing.size());
    for (const Config::ValidationEntry &entry : result.requiredMissing)
        reasons << entry.message;
    m_configSummary = reasons.join(QStringLiteral("; "));

    refreshWarningState();
}

void TrayMenu::refreshWarningState()
{
    const QString reason = warningTooltip(m_configValid, m_configSummary,
                                          m_slackConnected, m_proPresConnected);
    if (reason.isEmpty()) {
        m_tray->setIcon(m_normalIcon);
        m_tray->setToolTip(QStringLiteral("ProPager"));
    } else {
        // Warning variant + reason on hover, so the cause is available even if
        // the icon delta is subtle (step 6).
        m_tray->setIcon(m_warningIcon);
        m_tray->setToolTip(QStringLiteral("ProPager — %1").arg(reason));
    }
}

void TrayMenu::refreshActive()
{
    if (!m_pager)
        return;

    const QString active = m_pager->activeFormatted();
    if (active.isEmpty()) {
        m_activeAction->setText(QStringLiteral("Active Number: N/A"));
        return;
    }
    // Same singular/plural rule as the status window (see formatActiveLine).
    const QString plural =
        active.contains(QStringLiteral(" & ")) ? QStringLiteral("s") : QString();
    m_activeAction->setText(
        QStringLiteral("Active Number%1: %2").arg(plural, active));
}
