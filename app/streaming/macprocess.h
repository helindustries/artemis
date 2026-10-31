#pragma once

// Process-wide AppKit calls for stream processes, which the main window starts one per stream
namespace MacProcess
{
    // Keeps the stream process out of the Dock, so only the Artemis window that started it
    // shows there. Needs the QGuiApplication to exist.
    void hideFromDock();

    // Brings the process to the front; an accessory app is not activated by raising its window
    void activate();

    // How far down a fullscreen window starts on a screen: the menu bar band of a screen with a
    // camera notch, 0 without one. The screen is named by its left edge and size in points.
    int fullScreenTopInset(int x, int width, int height);
}
