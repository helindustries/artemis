#pragma once

#include <QObject>
#include <QVariantList>

// Named streaming setups per host: resolution, frame rate, bitrate, display mode and the
// screen to stream on. Kept apart from the hosts themselves so the stored host format
// stays the one upstream reads.
class StreamProfiles : public QObject
{
    Q_OBJECT

public:
    explicit StreamProfiles(QObject* parent = nullptr);

    // Each profile is a map with name, width, height, fps, bitrateKbps,
    // displayMode (fullscreen, borderless or windowed), screen (a QScreen name,
    // empty for the screen the Artemis window is on), app (what "all screens"
    // starts) and allScreens (whether "all screens" starts it).
    Q_INVOKABLE QVariantList profilesForHost(const QString& hostUuid) const;
    Q_INVOKABLE void setProfilesForHost(const QString& hostUuid, const QString& hostName,
                                        const QVariantList& profiles);

    // Every allScreens profile of every host whose screen is connected, as maps with
    // hostUuid, hostName, app and profile. A profile without a pinned screen is left
    // out: started together, they would all land on the same screen.
    Q_INVOKABLE QVariantList allScreensLaunches() const;

    // The connected screens with their size in points and in pixels, each rounded
    // down to even values since 4:2:0 video cannot be encoded at odd sizes.
    Q_INVOKABLE QVariantList screens() const;

signals:
    void profilesChanged(const QString& hostUuid);
};
