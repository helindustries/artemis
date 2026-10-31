#include "macprocess.h"

#import <AppKit/AppKit.h>

void MacProcess::hideFromDock()
{
    // An accessory app still shows windows and takes focus, it just has no Dock icon
    // or menu bar of its own
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

void MacProcess::activate()
{
    [NSApp activateIgnoringOtherApps:YES];
}

int MacProcess::fullScreenTopInset(int x, int width, int height)
{
    int inset = 0;
    if (@available(macOS 12.0, *)) {
        for (NSScreen* screen in [NSScreen screens]) {
            const NSRect frame = screen.frame;
            // A notch screen keeps the whole menu bar band (37 points on a 16" MacBook Pro), more
            // than the notch's safe area inset (32): the window gets what is below the menu bar
            if ((int)frame.origin.x == x && (int)frame.size.width == width && (int)frame.size.height == height &&
                    screen.safeAreaInsets.top > 0) {
                const NSRect visible = screen.visibleFrame;
                inset = (int)(NSMaxY(frame) - NSMaxY(visible));
            }
        }
    }
    return inset;
}
