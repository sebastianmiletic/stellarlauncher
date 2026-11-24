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
            if (std::getenv("STELLAR_RUST_FIT_WINDOW")) {
                [notifications addObserverForName:NSWindowDidBecomeMainNotification object:nil queue:nil usingBlock:^(NSNotification* event) {
                    NSWindow* window = event.object;
                    static NSHashTable<NSWindow*>* fitted = [NSHashTable weakObjectsHashTable];
                    if (window.screen && ![fitted containsObject:window] && (window.styleMask & NSWindowStyleMaskResizable)) {
                        [fitted addObject:window];
                        [window setFrame:window.screen.visibleFrame display:YES];
                    }
                }];
            }
            [notifications addObserverForName:NSWindowDidBecomeMainNotification object:nil queue:nil usingBlock:renameWindow];
            [notifications addObserverForName:NSWindowDidBecomeKeyNotification object:nil queue:nil usingBlock:renameWindow];
        });
    }
}
