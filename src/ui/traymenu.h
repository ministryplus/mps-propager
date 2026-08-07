#ifndef PROPAGER_UI_TRAYMENU_H
#define PROPAGER_UI_TRAYMENU_H

#include <QObject>
#include <QString>

class PagerController;
class QAction;
class QMenu;
class QSystemTrayIcon;

// TrayMenu is ProPager's menu-bar presence (Task 001-6), the native Qt6 port of
// widget.py. It owns a QSystemTrayIcon + QMenu mirroring the status window:
// disabled ProPresenter/Slack status lines, a disabled active-number line, an
// Open Window action, and Quit. It supersedes the placeholder tray created in
// Task 1 (that wiring moved out of main.cpp into here).
//
// Like MainWindow it is updated purely by signal/slot connections (Decision 7):
// no polling. Open Window is surfaced as a signal so main.cpp owns the window
// reference; Quit calls qApp->quit() directly.
class TrayMenu : public QObject
{
    Q_OBJECT

public:
    explicit TrayMenu(PagerController *pager, QObject *parent = nullptr);

public slots:
    void setSlackConnected(bool connected);
    void setProPresConnected(bool connected);
    // Re-render the active-number line from the controller.
    void refreshActive();

signals:
    // Emitted by the Open Window action; main.cpp shows/raises the window.
    void openWindowRequested();

private:
    PagerController *m_pager;
    QSystemTrayIcon *m_tray;
    QMenu *m_menu;
    QAction *m_proPresStatus;
    QAction *m_slackStatus;
    QAction *m_activeAction;
};

#endif // PROPAGER_UI_TRAYMENU_H
