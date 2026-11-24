// SPDX-License-Identifier: GPL-3.0-only

#import <AppKit/AppKit.h>

#include <cstdlib>
#include <cstring>

__attribute__((constructor)) static void useStellarWindowName()
{
    if (std::strcmp(getprogname(), "horizon-client") != 0) {
        return;
    }
    @autoreleasepool {
        dispatch_async(dispatch_get_main_queue(), ^{
            NSNotificationCenter* notifications = NSNotificationCenter.defaultCenter;
            void (^renameWindow)(NSNotification*) = ^(NSNotification* event) {
                NSWindow* window = event.object;
                NSString* title = window.title;
                if ([title isEqualToString:@"Horizon"] || [title hasPrefix:@"Horizon "]) {
                    window.title = [@"Stellar Minecraft" stringByAppendingString:[title substringFromIndex:@"Horizon".length]];
                }
            };
            [notifications addObserverForName:NSWindowDidBecomeMainNotification object:nil queue:nil usingBlock:renameWindow];
            [notifications addObserverForName:NSWindowDidBecomeKeyNotification object:nil queue:nil usingBlock:renameWindow];
        });
    }
}
