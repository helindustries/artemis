#pragma once

#include "backend/nvhttp.h"
#include "SDL_compat.h"

#include <QHash>
#include <QMutex>
#include <QRect>
#include <QVector>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

class NvComputer;

// The host's list for the atlas: its windows, front-most first, positions relative to all the
// client's screens; the stream's size; and whether the secure desktop (a UAC prompt) shows, for
// which the stream shows the host's primary display instead
struct AtlasLayout
{
    QVector<NvHostWindow> windows;
    QSize streamSize;
    bool secureDesktop = false;
};

// Coherence mode in one stream (macOS): the host draws every window of its coherence screen
// side by side into one picture, the atlas, and lists where each one is. Each host window gets
// a window here, where it is on the free screen area, showing its part of the picture. The
// stream process's own window stays invisible.
//
// Resizing a window here resizes its host window as it happens; the host's list then sizes the
// window here, so the host has the last word on sizes it does not accept.
class CoherenceAtlas
{
public:
    // A window's Metal layer (a CAMetalLayer*) and the part of the atlas it shows, in pixels;
    // a popup's is keyed, near-black shown transparent
    struct Target
    {
        void* metalLayer;
        QRect source;
        quint64 hostWindow;
        bool keyed;
    };

    CoherenceAtlas(const QRect& coherenceArea, NvComputer* computer);
    ~CoherenceAtlas();

    // The host's list: new windows get a window here, gone ones lose theirs, minimized ones hide;
    // while the secure desktop shows, one window on the primary screen shows the whole stream
    void apply(const AtlasLayout& layout);

    // The host window an SDL window shows, 0 when it is none of ours
    quint64 hostWindow(Uint32 sdlWindowId) const;
    bool isPopup(Uint32 sdlWindowId) const;

    // Whether a host menu, tooltip or Start shows here
    bool hasPopups() const;

    // A program's Dock icon was activated: its windows come to the front, here and on the host,
    // minimized ones restored
    void raiseProgram(const QString& program);

    // Main thread, on a window of ours taking focus: AppKit finishes activating Artemis after
    // raiseProgram() and brings back the window that was key before. True when that happened
    // and the program's windows went to the front again, so the host keeps them in front too
    bool finishRaise();

    // A program's Dock icon was quit: its host windows are asked to close
    void closeProgram(const QString& program);

    // Activates Artemis for something else than one of its windows (a tray icon, Start) with
    // the host's front window in front and key, as AppKit would otherwise bring back the window
    // that last was
    void activateFront();

    // The host's front-most window as last listed, or as last raised from here since
    quint64 frontWindow() const { return m_FrontWindow; }
    void setFrontWindow(quint64 id) { m_FrontWindow = id; }

    // Keeps a stream process's own window out of sight and out of the way of the pointer
    static void hide(SDL_Window* window);

    // Any thread, before activating Artemis for something else than one of its windows (a tray
    // icon, Start, a window just shown): AppKit gives focus back to the window that last had
    // it, whose host window must not come to the front for that. Main thread: whether that is
    // still to come
    static void holdFocusRaise();
    static bool focusRaiseHeld();

    // Any thread: where the listed windows are in the atlas now. AppKit holds the main thread
    // while a window is resized, so the picture follows the host's resize from here
    static void refreshSources(const QVector<NvHostWindow>& windows);

    // Render thread: the windows to draw a frame into, their layers retained until
    // releaseTargets(); empty while no atlas runs
    static bool isActive();
    static QVector<Target> takeTargets();
    static void releaseTargets(const QVector<Target>& targets);

private:
    struct Shown
    {
        SDL_Window* window;
        SDL_MetalView view;
        NvHostWindow host;
        // Visible: the stream has drawn its picture into it, see REVEAL_AFTER_FRAMES
        bool revealed;
    };

    void show(const NvHostWindow& host, const QRect& frame);
    void revealDrawn();
    void orderLikeHost();
    void showSecureDesktop(const QSize& streamSize);
    void updateTargets();

    // Called while AppKit resizes a window, before the session's event loop runs again
    static int SDLCALL watchResize(void* atlas, SDL_Event* event);
    void sendFrames();

    QRect m_CoherenceArea;
    // Every screen's free area, which a maximized host window fills
    QVector<QRect> m_Screens;
    NvComputer* m_Computer;
    QHash<quint64, Shown> m_Shown;
    // The window showing the whole stream while the secure desktop shows, null otherwise
    Shown m_SecureDesktop {};
    QSize m_StreamSize;
    // The host's windows as last listed, front-most first; minimized ones may have no window here
    QVector<NvHostWindow> m_Listed;
    // The shown windows' order on the host, front first, as AppKit's was last made to follow
    QVector<quint64> m_Order;
    quint64 m_FrontWindow = 0;
    // The windows raiseProgram() or activateFront() brought forward, back to front, until
    // activation is done
    QVector<quint64> m_PendingRaise;
    Uint32 m_PendingRaiseDeadline = 0;
    // activateFront() raised: the host's order holds, AppKit's follows it again
    bool m_PendingReorder = false;

    // Host window frames to send, relative to the coherence area, newest per window
    std::mutex m_FrameLock;
    std::condition_variable m_FrameWake;
    QHash<quint64, QRect> m_PendingFrames;
    bool m_Stopping = false;
    std::thread m_FrameSender;

    static std::atomic<Uint32> s_FocusRaiseHeldUntil;
    static QMutex s_TargetLock;
    static QVector<Target> s_Targets;
    // Frames the renderer drew into each host window's window, under s_TargetLock
    static QHash<quint64, int> s_FramesDrawn;
    static bool s_Active;
};
