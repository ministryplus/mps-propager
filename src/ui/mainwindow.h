#ifndef PROPAGER_UI_MAINWINDOW_H
#define PROPAGER_UI_MAINWINDOW_H

#include <QList>
#include <QMainWindow>
#include <QString>

#include "config.h"         // for Config::ValidationResult in the banner API
#include "pagercontroller.h" // for PagerController::Batch in the static API

class QLabel;
class QPlainTextEdit;
class QFrame;
class QCloseEvent;
class ConnectionsTab;

// MainWindow is ProPager's status window (Task 001-6), the native Qt6 port of
// overview.py + mainwindow.py. A QTabWidget holds two tabs:
//   - Overview: the active number, the queued-numbers list, and a status bar
//     reporting Slack and ProPresenter connection state.
//   - Log: a scrolling, read-only view of the app's error/access log, plus
//     buttons to reveal the on-disk log file and clear the view.
//
// Decision 7: every update arrives as a direct signal/slot connection — the
// poll_in_thread status thread from overview.py is deliberately NOT ported.
// The Log tab replaces the modal error dialog (a QMessageBox spins a nested
// event loop, so bursts of errors stacked modals and could wedge the UI); it
// makes an otherwise-unactionable "Disconnected" status actionable by showing
// the actual failure text.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // `config` backs the Connections tab (Task 002-4); `logDir` is the directory
    // holding both ProPager.log and the .ini (Config::configDir()) — the Log
    // tab's Reveal buttons open it.
    explicit MainWindow(PagerController *pager, Config *config,
                        const QString &logDir, QWidget *parent = nullptr);

    // --- Validation banner (Task 002-5, Spec 002 Decision 8) ---------------

    // The banner's two visible tiers plus a hidden state. Required-but-unset
    // config is a hard blocker (Error, "must fix"); present-but-malformed is
    // warn-only (Warning, "looks off"); a clean result hides the banner (None).
    enum class BannerTier { None, Error, Warning };
    Q_ENUM(BannerTier)

    // The pure decision behind showValidation(): what the banner should say and
    // in which tier, given a validation result. Unit-tested guiless (mirrors
    // TrayMenu::warningTooltip); showValidation() only applies it to widgets.
    struct BannerContent
    {
        BannerTier tier;
        QString message;
    };
    static BannerContent bannerContent(const Config::ValidationResult &result);

    // The owned Connections tab (Task 002-4). main.cpp (Task 002-7) connects its
    // saveRequested/reconnectRequested/testRequested signals through this
    // accessor so all side-effect wiring stays in the integrator.
    ConnectionsTab *connectionsTab() const { return m_connectionsTab; }

    // --- Pure formatting (ports overview.py; unit-tested directly) ---------

    // The active/last block. Ports overview.py: a "Last Number: N" line is
    // prepended when `lastNumber` is set; the active line pluralizes to
    // "Active Numbers:" for a multi-number batch (detected by the " & " that
    // PagerController::formatBatch places before the final number) and shows
    // "Active Number: N/A" when nothing is on screen.
    static QString formatActiveLine(const QString &lastNumber,
                                    const QString &activeFormatted);
    // The "Numbers Queued:" block: each batch formatted via
    // PagerController::formatBatch, newline-joined under the header.
    static QString formatQueue(const QList<PagerController::Batch> &batches);

public slots:
    void setSlackConnected(bool connected);
    void setProPresConnected(bool connected);
    // Re-render the active line and queue list from the controller.
    void refresh();
    // Append one log line to the Log tab (connect LogBroker::appended here).
    void appendLog(const QString &line);
    // Render `result` into the window-level banner above the tabs: hide it when
    // clean, error-styled for required-missing, warning-styled for shape-only.
    void showValidation(const Config::ValidationResult &result);

protected:
    // Hide to the tray instead of quitting (menu-bar app); only Quit exits.
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *buildOverviewTab();
    QWidget *buildConnectionsTab();
    QWidget *buildLogTab();

    PagerController *m_pager;
    Config *m_config;
    QString m_logDir;
    QLabel *m_activeLabel;
    QLabel *m_queueLabel;
    QLabel *m_slackStatus;
    QLabel *m_proPresStatus;
    QPlainTextEdit *m_logView;
    ConnectionsTab *m_connectionsTab = nullptr;
    QFrame *m_banner = nullptr;
    QLabel *m_bannerLabel = nullptr;
};

#endif // PROPAGER_UI_MAINWINDOW_H
