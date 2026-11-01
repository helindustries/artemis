#include "coherencetray.h"

#include "backend/computermanager.h"
#include "SDL_compat.h"

#import <AppKit/AppKit.h>

// The host's icons change rarely; a click polls at once
#define TRAY_POLL_MS 2000

// Menu bar icons are drawn at this size in points; the host's are 20 px at 100%
#define TRAY_ICON_POINTS 18

@interface CoherenceTrayTarget : NSObject
{
@public
    std::function<void(int, bool)> onClick;
}
- (void)clicked:(NSStatusBarButton*)button;
@end

@implementation CoherenceTrayTarget
- (void)clicked:(NSStatusBarButton*)button
{
    const NSEventType type = NSApp.currentEvent.type;
    onClick((int)button.tag, type == NSEventTypeRightMouseUp || type == NSEventTypeRightMouseDown);
}
@end

CoherenceTray::CoherenceTray(NvComputer* computer, std::function<void()> activate)
    : m_Computer(computer),
      m_Activate(std::move(activate))
{
    CoherenceTrayTarget* target = [[CoherenceTrayTarget alloc] init];
    target->onClick = [this](int index, bool right) { click(index, right); };
    m_Target = target;
    m_Items = [[NSMutableArray alloc] init];
    m_Poller = std::thread([this]() { poll(); });
}

CoherenceTray::~CoherenceTray()
{
    {
        std::lock_guard lock(m_Lock);
        m_Stopping = true;
    }
    m_Wake.notify_all();
    m_Poller.join();

    NSMutableArray* items = (NSMutableArray*)m_Items;
    for (NSStatusItem* item in items) {
        [[NSStatusBar systemStatusBar] removeStatusItem:item];
    }
    [items release];
    [(CoherenceTrayTarget*)m_Target release];
}

void CoherenceTray::poll()
{
    bool failed = false;
    std::unique_lock lock(m_Lock);
    while (!m_Stopping) {
        const QVector<QPair<int, bool>> clicks = m_Clicks;
        m_Clicks.clear();
        lock.unlock();
        for (const auto& [index, right] : clicks) {
            try {
                NvHTTP(m_Computer).clickTrayIcon(index, right);
            } catch (const GfeHttpResponseException& e) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Expected the host's tray icon %d to take a click, but: %s", index, qPrintable(e.toQString()));
            } catch (const QtNetworkReplyException& e) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Expected the host's tray icon %d to take a click, but: %s", index, qPrintable(e.toQString()));
            }
        }

        QVector<NvTrayIcon> icons;
        bool polled = false;
        try {
            icons = NvHTTP(m_Computer).getTrayIcons();
            polled = true;
            failed = false;
        } catch (const GfeHttpResponseException& e) {
            if (!failed) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Host tray icons unavailable: %s", qPrintable(e.toQString()));
            }
            failed = true;
        } catch (const QtNetworkReplyException& e) {
            if (!failed) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Host tray icons unavailable: %s", qPrintable(e.toQString()));
            }
            failed = true;
        }
        lock.lock();

        if (polled) {
            const bool unapplied = m_Pending;
            m_Latest = icons;
            m_Pending = true;
            if (!unapplied) {
                SDL_Event event = {};
                event.type = SDL_USEREVENT;
                event.user.code = SDL_CODE_TRAY_ICONS;
                SDL_PushEvent(&event);
            }
        }
        m_Wake.wait_for(lock, std::chrono::milliseconds(TRAY_POLL_MS), [this]() { return m_Stopping || !m_Clicks.isEmpty(); });
    }
}

void CoherenceTray::applyLatest()
{
    QVector<NvTrayIcon> icons;
    {
        std::lock_guard lock(m_Lock);
        icons = m_Latest;
        m_Pending = false;
    }

    // The menu bar adds each new item left of the others: created right to left, they keep the
    // host's order. A changed count starts over, as the indexes clicks name have moved
    NSMutableArray* items = (NSMutableArray*)m_Items;
    if (icons.size() != m_Shown.size()) {
        for (NSStatusItem* item in items) {
            [[NSStatusBar systemStatusBar] removeStatusItem:item];
        }
        [items removeAllObjects];
        for (int index = (int)icons.size() - 1; index >= 0; --index) {
            NSStatusItem* item = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
            item.button.tag = index;
            item.button.target = (CoherenceTrayTarget*)m_Target;
            item.button.action = @selector(clicked:);
            [item.button sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
            [items insertObject:item atIndex:0];
        }
        m_Shown = QVector<NvTrayIcon>(icons.size());
    }

    for (int index = 0; index < icons.size(); ++index) {
        NSStatusItem* item = items[index];
        if (icons[index].png != m_Shown[index].png) {
            NSData* data = [NSData dataWithBytes:icons[index].png.constData() length:icons[index].png.size()];
            NSImage* image = [[NSImage alloc] initWithData:data];
            image.size = NSMakeSize(TRAY_ICON_POINTS, TRAY_ICON_POINTS);
            item.button.image = image;
            [image release];
        }
        if (icons[index].name != m_Shown[index].name) {
            item.button.toolTip = icons[index].name.toNSString();
        }
    }
    m_Shown = icons;
}

void CoherenceTray::click(int index, bool right)
{
    // The icon's menu opens on the host as a popup here, which closes when Artemis loses focus:
    // Artemis has to have it first
    m_Activate();
    {
        std::lock_guard lock(m_Lock);
        m_Clicks.append(qMakePair(index, right));
    }
    m_Wake.notify_all();
}
