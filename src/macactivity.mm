#include "macactivity.h"

#include <QDebug>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

namespace {

// The activity token; App Nap stays disabled for as long as it is held, so it
// is retained for the life of the process and never ended.
id<NSObject> g_activity = nil;

} // namespace

void MacActivity::preventAppNap(const QString &reason)
{
    if (g_activity)
        return;
    @autoreleasepool {
        g_activity = [[[NSProcessInfo processInfo]
            beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
                              reason:reason.toNSString()] retain];
    }
    qInfo().noquote() << "[app] App Nap disabled:" << reason;
}

void MacActivity::logSleepWake()
{
    @autoreleasepool {
        NSNotificationCenter *center =
            [[NSWorkspace sharedWorkspace] notificationCenter];
        [center addObserverForName:NSWorkspaceWillSleepNotification
                            object:nil
                             queue:[NSOperationQueue mainQueue]
                        usingBlock:^(NSNotification *) {
                            qInfo() << "[system] Mac is going to sleep";
                        }];
        [center addObserverForName:NSWorkspaceDidWakeNotification
                            object:nil
                             queue:[NSOperationQueue mainQueue]
                        usingBlock:^(NSNotification *) {
                            qInfo() << "[system] Mac woke from sleep";
                        }];
    }
}
