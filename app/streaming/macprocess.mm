#include "macprocess.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

// Measured on a mirrored Explorer at 2x: Windows' 1 px outline arrives smeared over 2 points by
// the stream's scaling, and the corners follow a 10 px radius (8 px at the coherence screen's
// 125%) whose centre sits 11 points in from the window's edges
static const CGFloat HOST_WINDOW_OUTLINE = 2;
static const CGFloat HOST_WINDOW_CORNER_RADIUS = 11;

void MacProcess::showInDock()
{
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
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

void MacProcess::shapeLikeHostWindow(void* nsWindow)
{
    NSWindow* window = (__bridge NSWindow*)nsWindow;
    window.opaque = NO;
    window.backgroundColor = NSColor.clearColor;
    window.hasShadow = YES;

    // The mask follows the content view's size, so a resized window keeps its shape; the
    // renderer's view is a subview, its layer a sublayer, and masked with it
    NSView* content = window.contentView;
    content.wantsLayer = YES;
    CALayer* mask = [CALayer layer];
    mask.backgroundColor = NSColor.blackColor.CGColor;
    mask.cornerRadius = HOST_WINDOW_CORNER_RADIUS - HOST_WINDOW_OUTLINE;
    mask.frame = NSInsetRect(content.bounds, HOST_WINDOW_OUTLINE, HOST_WINDOW_OUTLINE);
    mask.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    content.layer.mask = mask;
    [window invalidateShadow];
}
