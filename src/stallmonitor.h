#ifndef PROPAGER_STALLMONITOR_H
#define PROPAGER_STALLMONITOR_H

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

// StallMonitor notices when the event loop ran late: a timer meant to fire every
// intervalMs arrived more than thresholdMs behind schedule. On the booth Macs
// that means App Nap throttled the app (or something blocked the loop), which
// also delays Slack pongs and page handling — so it is logged as evidence.
//
// It measures with QElapsedTimer, which on macOS does not advance while the
// system sleeps; sleep itself is logged separately (macactivity.h), so a stall
// line always means "awake but held up".
class StallMonitor : public QObject
{
    Q_OBJECT

public:
    explicit StallMonitor(int intervalMs = 5000, int thresholdMs = 5000,
                          QObject *parent = nullptr);

    void start();

signals:
    void stalled(qint64 lateMs);

private:
    void tick();

    int m_intervalMs;
    int m_thresholdMs;
    QTimer m_timer;
    QElapsedTimer m_sinceTick;
};

#endif // PROPAGER_STALLMONITOR_H
