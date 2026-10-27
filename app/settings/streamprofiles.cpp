#include "streamprofiles.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSettings>

#define SER_STREAMPROFILES "streamprofiles"

static const QStringList k_ProfileKeys = {
    "name", "width", "height", "fps", "bitrateKbps", "displayMode", "screen"
};

StreamProfiles::StreamProfiles(QObject* parent)
    : QObject(parent)
{
}

QVariantList StreamProfiles::profilesForHost(const QString& hostUuid) const
{
    QSettings settings;
    QVariantList profiles;

    settings.beginGroup(SER_STREAMPROFILES);
    int count = settings.beginReadArray(hostUuid);
    for (int i = 0; i < count; i++) {
        settings.setArrayIndex(i);

        QVariantMap profile;
        for (const QString& key : k_ProfileKeys) {
            profile[key] = settings.value(key);
        }
        profiles.append(profile);
    }
    settings.endArray();
    settings.endGroup();

    return profiles;
}

void StreamProfiles::setProfilesForHost(const QString& hostUuid, const QVariantList& profiles)
{
    QSettings settings;

    settings.beginGroup(SER_STREAMPROFILES);
    settings.remove(hostUuid);
    settings.beginWriteArray(hostUuid);
    for (int i = 0; i < profiles.size(); i++) {
        settings.setArrayIndex(i);

        QVariantMap profile = profiles[i].toMap();
        for (const QString& key : k_ProfileKeys) {
            settings.setValue(key, profile.value(key));
        }
    }
    settings.endArray();
    settings.endGroup();

    emit profilesChanged(hostUuid);
}

QVariantList StreamProfiles::screens() const
{
    QVariantList screens;

    for (QScreen* screen : QGuiApplication::screens()) {
        QSize points = screen->size();
        QSize pixels = points * screen->devicePixelRatio();

        QVariantMap entry;
        entry["name"] = screen->name();
        entry["width"] = points.width() & ~1;
        entry["height"] = points.height() & ~1;
        entry["nativeWidth"] = pixels.width() & ~1;
        entry["nativeHeight"] = pixels.height() & ~1;
        entry["refreshRate"] = qRound(screen->refreshRate());
        screens.append(entry);
    }

    return screens;
}
