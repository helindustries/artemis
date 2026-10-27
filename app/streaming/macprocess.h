#pragma once

// Process-wide AppKit calls for stream processes, which the main window starts one per stream
namespace MacProcess
{
    // Keeps the stream process out of the Dock, so only the Artemis window that started it
    // shows there. Needs the QGuiApplication to exist.
    void hideFromDock();

    // Brings the process to the front; an accessory app is not activated by raising its window
    void activate();
}
