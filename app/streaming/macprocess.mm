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
