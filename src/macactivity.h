#ifndef PROPAGER_MACACTIVITY_H
#define PROPAGER_MACACTIVITY_H

#include <QString>

// macOS process-activity hooks (implemented in macactivity.mm; macOS only).
namespace MacActivity {

// Opt the whole process out of App Nap for its lifetime, via Apple's supported
// NSProcessInfo activity API. ProPager is a background listener: when its
// window is hidden macOS would otherwise nap it, throttling the timers and
// socket handling that Slack pages and the connection heartbeat depend on.
// Idle *system* sleep is still allowed — the booth Macs keep themselves awake
// (Amphetamine), and the app should not fight the operator's power settings.
void preventAppNap(const QString &reason);

// Log the Mac going to sleep and waking, so a gap in the log can be told
// apart from App Nap or a dead connection.
void logSleepWake();

} // namespace MacActivity

#endif // PROPAGER_MACACTIVITY_H
