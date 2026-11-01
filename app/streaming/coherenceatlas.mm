#include "coherenceatlas.h"
#include "macprocess.h"
#include "backend/coherencemanager.h"

#include <SDL_syswm.h>

#include <QSet>

#include <algorithm>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

QMutex CoherenceAtlas::s_TargetLock;
QVector<CoherenceAtlas::Target> CoherenceAtlas::s_Targets;
bool CoherenceAtlas::s_Active = false;

static NSWindow* cocoaWindow(SDL_Window* window)
{
    NSWindow* cocoa = nil;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_COCOA) {
        cocoa = info.info.cocoa.window;
    }
    return cocoa;
}

// A window as large as a screen's free area is maximized: Windows draws it without outline or
// rounded corners, and a shaped window would show the desktop through its edges
static void shapeLikeHostWindow(SDL_Window* window, const QSize& size, const QVector<QRect>& screens)
{
    if (NSWindow* cocoa = cocoaWindow(window); cocoa != nil) {
        if (std::any_of(screens.cbegin(), screens.cend(), [&size](const QRect& screen) {
                return qAbs(size.width() - screen.width()) <= 1 && qAbs(size.height() - screen.height()) <= 1;
            })) {
            cocoa.contentView.layer.mask = nil;
            cocoa.opaque = YES;
        }
        else {
            MacProcess::shapeLikeHostWindow(cocoa);
        }
    }
}

CoherenceAtlas::CoherenceAtlas(const QRect& coherenceArea, NvComputer* computer)
    : m_CoherenceArea(coherenceArea),
      m_Screens(CoherenceManager::screenAreas()),
      m_Computer(computer)
{
    {
        QMutexLocker lock(&s_TargetLock);
        s_Active = true;
    }
    m_FrameSender = std::thread([this]() { sendFrames(); });
    SDL_AddEventWatch(watchResize, this);
}

CoherenceAtlas::~CoherenceAtlas()
{
    SDL_DelEventWatch(watchResize, this);
    {
        std::lock_guard lock(m_FrameLock);
        m_Stopping = true;
    }
    m_FrameWake.notify_one();
    m_FrameSender.join();
    {
        QMutexLocker lock(&s_TargetLock);
        s_Targets.clear();
        s_Active = false;
    }
    for (const Shown& shown : m_Shown) {
        SDL_Metal_DestroyView(shown.view);
        SDL_DestroyWindow(shown.window);
    }
}

void CoherenceAtlas::hide(SDL_Window* window)
{
    if (NSWindow* cocoa = cocoaWindow(window); cocoa != nil) {
        cocoa.alphaValue = 0;
        cocoa.hasShadow = NO;
        cocoa.ignoresMouseEvents = YES;
        cocoa.collectionBehavior |= NSWindowCollectionBehaviorTransient | NSWindowCollectionBehaviorIgnoresCycle;
    }
}

void CoherenceAtlas::show(const NvHostWindow& host, const QRect& frame)
{
    // Popups stay above the windows, as menus and tooltips do on the host. Windows take AppKit's
    // resize edges and cursors, which the host's own borders do not show here
    Uint32 flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI;
    if (host.popup) {
        flags |= SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_SKIP_TASKBAR;
    }
    else {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (SDL_Window* window = SDL_CreateWindow(host.title.toUtf8().constData(), frame.x(), frame.y(), frame.width(), frame.height(), flags);
            window != nullptr) {
        if (SDL_MetalView view = SDL_Metal_CreateView(window); view != nullptr) {
            shapeLikeHostWindow(window, frame.size(), m_Screens);
            m_Shown.insert(host.id, { window, view, host });
        }
        else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Expected a Metal view for the window of host window %llu, but: %s",
                         host.id, SDL_GetError());
            SDL_DestroyWindow(window);
        }
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Expected a window for host window %llu, but: %s",
                     host.id, SDL_GetError());
    }
}

void CoherenceAtlas::apply(const QVector<NvHostWindow>& windows)
{
    QSet<quint64> listed;
    m_FrontWindow = 0;
    m_Listed = windows;
    for (const NvHostWindow& host : windows) {
        listed.insert(host.id);
        if (m_FrontWindow == 0 && !host.popup && !host.minimized) {
            m_FrontWindow = host.id;
        }
        const QRect frame(m_CoherenceArea.x() + host.x, m_CoherenceArea.y() + host.y, host.width, host.height);
        // A minimized host window hides behind its program's Dock icon, not in a thumbnail
        if (auto shown = m_Shown.find(host.id); shown != m_Shown.end()) {
            const bool hidden = (SDL_GetWindowFlags(shown->window) & SDL_WINDOW_HIDDEN) != 0;
            if (host.minimized && !hidden) {
                SDL_HideWindow(shown->window);
            }
            else if (!host.minimized) {
                if (hidden) {
                    SDL_ShowWindow(shown->window);
                }
                const NvHostWindow& last = shown->host;
                if (host.x != last.x || host.y != last.y) {
                    SDL_SetWindowPosition(shown->window, frame.x(), frame.y());
                }
                if (host.width != last.width || host.height != last.height) {
                    SDL_SetWindowSize(shown->window, frame.width(), frame.height());
                    shapeLikeHostWindow(shown->window, frame.size(), m_Screens);
                }
                if (host.title != last.title) {
                    SDL_SetWindowTitle(shown->window, host.title.toUtf8().constData());
                }
            }
            shown->host = host;
        }
        else if (!host.minimized && host.atlas.isValid()) {
            show(host, frame);
        }
    }

    // Gone windows leave the renderer's targets before their layers go with them
    QVector<Shown> gone;
    for (auto shown = m_Shown.begin(); shown != m_Shown.end();) {
        if (!listed.contains(shown.key())) {
            gone.append(*shown);
            shown = m_Shown.erase(shown);
        }
        else {
            ++shown;
        }
    }
    updateTargets();
    for (const Shown& shown : gone) {
        SDL_Metal_DestroyView(shown.view);
        SDL_DestroyWindow(shown.window);
    }
}

quint64 CoherenceAtlas::hostWindow(Uint32 sdlWindowId) const
{
    quint64 id = 0;
    for (auto shown = m_Shown.cbegin(); shown != m_Shown.cend(); ++shown) {
        if (SDL_GetWindowID(shown->window) == sdlWindowId) {
            id = shown.key();
        }
    }
    return id;
}

void CoherenceAtlas::raiseProgram(const QString& program)
{
    // Back to front, so the program's front-most window ends up in front on both sides; a window
    // minimized since before this stream gets its window here with the next list
    QVector<quint64> raised;
    for (auto host = m_Listed.crbegin(); host != m_Listed.crend(); ++host) {
        if (host->process == program && !host->popup) {
            try {
                NvHTTP(m_Computer).raiseWindow(host->id);
                raised.append(host->id);
            } catch (const GfeHttpResponseException& e) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Expected host window %llu to come to the front, but: %s", host->id, qPrintable(e.toQString()));
            } catch (const QtNetworkReplyException& e) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Expected host window %llu to come to the front, but: %s", host->id, qPrintable(e.toQString()));
            }
            if (const auto shown = m_Shown.constFind(host->id); shown != m_Shown.cend()) {
                SDL_ShowWindow(shown->window);
                SDL_RaiseWindow(shown->window);
            }
        }
    }
    if (!raised.isEmpty()) {
        m_FrontWindow = raised.last();
        MacProcess::activate();
    }
}

bool CoherenceAtlas::hasPopups() const
{
    return std::any_of(m_Shown.cbegin(), m_Shown.cend(), [](const Shown& shown) { return shown.host.popup; });
}

bool CoherenceAtlas::isPopup(Uint32 sdlWindowId) const
{
    const quint64 id = hostWindow(sdlWindowId);
    return id != 0 && m_Shown.value(id).host.popup;
}

void CoherenceAtlas::updateTargets()
{
    QVector<Target> targets;
    for (auto shown = m_Shown.cbegin(); shown != m_Shown.cend(); ++shown) {
        if (!shown->host.minimized && shown->host.atlas.isValid()) {
            targets.append({ SDL_Metal_GetLayer(shown->view), shown->host.atlas, shown.key() });
        }
    }
    QMutexLocker lock(&s_TargetLock);
    s_Targets = targets;
}

void CoherenceAtlas::refreshSources(const QVector<NvHostWindow>& windows)
{
    QMutexLocker lock(&s_TargetLock);
    for (Target& target : s_Targets) {
        for (const NvHostWindow& window : windows) {
            if (window.id == target.hostWindow && window.atlas.isValid()) {
                target.source = window.atlas;
            }
        }
    }
}

int SDLCALL CoherenceAtlas::watchResize(void* atlas, SDL_Event* event)
{
    // RESIZED comes only from outside SDL: the user dragging an edge
    if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_RESIZED) {
        auto* self = static_cast<CoherenceAtlas*>(atlas);
        // AppKit's frame, flipped to the top-left origin: SDL's position reads 0,0 in this callback
        if (const quint64 id = self->hostWindow(event->window.windowID); id != 0) {
            if (NSWindow* cocoa = cocoaWindow(self->m_Shown.value(id).window); cocoa != nil) {
                const NSRect frame = cocoa.frame;
                const CGFloat primaryHeight = NSScreen.screens.firstObject.frame.size.height;
                const int x = (int)frame.origin.x;
                const int y = (int)(primaryHeight - frame.origin.y - frame.size.height);
                {
                    std::lock_guard lock(self->m_FrameLock);
                    self->m_PendingFrames.insert(id, QRect(x - self->m_CoherenceArea.x(), y - self->m_CoherenceArea.y(),
                                                           event->window.data1, event->window.data2));
                }
                self->m_FrameWake.notify_one();
            }
        }
    }
    return 1;
}

void CoherenceAtlas::sendFrames()
{
    // One request at a time, the newest frame of each window: a live resize asks far faster
    // than the host answers
    std::unique_lock lock(m_FrameLock);
    while (!m_Stopping) {
        m_FrameWake.wait(lock, [this]() { return m_Stopping || !m_PendingFrames.isEmpty(); });
        if (!m_Stopping) {
            const quint64 id = m_PendingFrames.constBegin().key();
            const QRect frame = m_PendingFrames.take(id);
            lock.unlock();
            try {
                const QPoint position = frame.topLeft();
                NvHTTP(m_Computer).resizeWindow(id, frame.width(), frame.height(), &position);
            } catch (const GfeHttpResponseException& e) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Expected host window %llu to take its new size, but: %s", id, qPrintable(e.toQString()));
            } catch (const QtNetworkReplyException& e) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Expected host window %llu to take its new size, but: %s", id, qPrintable(e.toQString()));
            }
            lock.lock();
        }
    }
}

bool CoherenceAtlas::isActive()
{
    QMutexLocker lock(&s_TargetLock);
    return s_Active;
}

QVector<CoherenceAtlas::Target> CoherenceAtlas::takeTargets()
{
    QMutexLocker lock(&s_TargetLock);
    for (const Target& target : s_Targets) {
        CFRetain(target.metalLayer);
    }
    return s_Targets;
}

void CoherenceAtlas::releaseTargets(const QVector<Target>& targets)
{
    for (const Target& target : targets) {
        CFRelease(target.metalLayer);
    }
}
