#ifndef PROPAGER_BACKOFF_H
#define PROPAGER_BACKOFF_H

#include <QtGlobal>

// Exponential reconnect backoff shared by the Slack and ProPresenter clients:
// 0 -> base, else doubled up to a cap.
inline int nextBackoffMs(int currentMs, int baseMs = 1000, int maxMs = 30000)
{
    if (currentMs <= 0)
        return baseMs;
    return qMin(currentMs * 2, maxMs);
}

#endif // PROPAGER_BACKOFF_H
