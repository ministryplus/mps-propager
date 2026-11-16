#include "stallmonitor.h"

StallMonitor::StallMonitor(int intervalMs, int thresholdMs, QObject *parent)
    : QObject(parent), m_intervalMs(intervalMs), m_thresholdMs(thresholdMs)
{
    // PreciseTimer: the default coarse timer may legitimately slip ~5%, which
    // must not read as a stall.
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(m_intervalMs);
    connect(&m_timer, &QTimer::timeout, this, &StallMonitor::tick);
}

void StallMonitor::start()
{
    m_sinceTick.start();
    m_timer.start();
}

void StallMonitor::tick()
{
    const qint64 lateMs = m_sinceTick.restart() - m_intervalMs;
    if (lateMs >= m_thresholdMs)
        emit stalled(lateMs);
}
