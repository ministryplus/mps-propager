#ifndef PROPAGER_LOGBROKER_H
#define PROPAGER_LOGBROKER_H

#include <QObject>
#include <QString>
#include <QtGlobal> // QtMsgType

// LogBroker is the single hop between Qt's global message handler (a free
// function installed in main) and the in-window Log tab. The handler posts every
// formatted line here; MainWindow's log view connects to appended().
//
// This replaces the modal error dialogs from the first UI cut: a QMessageBox
// spins a nested event loop per error, so a burst of client error() signals
// stacked modals and could wedge the UI. Appending to a log view is non-blocking
// and keeps the "why" (connection failures, reconnect backoff, failed reactions)
// visible and actionable instead of an unactionable "Disconnected" status.
class LogBroker : public QObject
{
    Q_OBJECT

public:
    // Process-wide instance, created in the GUI thread on first use.
    static LogBroker *instance();

    // "[LEVEL] message" — the shared on-screen/on-disk line format. Pure and
    // unit-tested so the log format cannot silently drift.
    static QString formatLine(QtMsgType type, const QString &message);

    // Emit appended(line). Safe to call from the message handler; receivers
    // should connect with a queued connection so delivery hops to the GUI thread.
    void post(const QString &line);

signals:
    void appended(const QString &line);

private:
    LogBroker() = default;
};

#endif // PROPAGER_LOGBROKER_H
