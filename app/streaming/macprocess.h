#pragma once

// Process-wide AppKit calls for stream processes, which the main window starts one per stream
namespace MacProcess
{
    // Artemis.app is an agent app (LSUIElement), so a stream process the Artemis window starts
    // never shows in the Dock or its recent apps; every other process makes itself a regular
    // app with this. Needs the QGuiApplication to exist.
    void showInDock();

    // Brings the process to the front; an accessory app is not activated by raising its window
    void activate();

    // How far down a fullscreen window starts on a screen: the menu bar band of a screen with a
    // camera notch, 0 without one. The screen is named by its left edge and size in points.
    int fullScreenTopInset(int x, int width, int height);

    // Gives a coherence window (an NSWindow*) the host window's shape: Windows 11 draws a 1 px
    // outline and 8 px rounded corners, both partly transparent, which the stream turns black
    void shapeLikeHostWindow(void* nsWindow);
}
