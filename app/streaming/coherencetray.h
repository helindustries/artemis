#pragma once

#include "backend/nvhttp.h"

#include <QPair>
#include <QVector>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

class NvComputer;

// SDL_USEREVENT code of a newer tray icon list, which applyLatest() takes on the main thread
#define SDL_CODE_TRAY_ICONS 113

// Coherence mode's tray (macOS): the host's pinned tray icons as menu bar items, in the host's
// order, polled on a thread of their own. A click on one clicks the icon on the host, which
// opens its window or menu there, shown here as host windows are.
class CoherenceTray
{
public:
    // activate: main thread, makes Artemis active before a click, for the menu it opens
    CoherenceTray(NvComputer* computer, std::function<void()> activate);
    ~CoherenceTray();

    // Main thread: shows the newest list polled
    void applyLatest();

private:
    void poll();
    void click(int index, bool right);

    NvComputer* m_Computer;
    std::function<void()> m_Activate;

    // m_Latest is a list not yet applied while m_Pending; m_Clicks wait for the poller, which
    // sends them (index, right button) and polls at once
    std::mutex m_Lock;
    std::condition_variable m_Wake;
    bool m_Stopping = false;
    bool m_Pending = false;
    QVector<NvTrayIcon> m_Latest;
    QVector<QPair<int, bool>> m_Clicks;
    std::thread m_Poller;

    // Main thread only: the NSStatusItems (an NSMutableArray*, retained) and the click target
    void* m_Items = nullptr;
    void* m_Target = nullptr;
    QVector<NvTrayIcon> m_Shown;
};
