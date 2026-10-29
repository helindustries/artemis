#include "streamprofiles.h"

#include <QGuiApplication>
#include <QMap>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <QUuid>

#define SER_STREAMPROFILES "streamprofiles"
#define SER_HOSTNAMES "hostnames"

static const QStringList k_ProfileKeys = {
    "id", "name", "width", "height", "fps", "bitrateKbps", "displayMode", "screen", "app", "allScreens"
};

// Gives every profile without one an id, which it keeps across edits: the host ties the
// profile's virtual display to it
static void writeProfiles(QSettings& settings, const QString& hostUuid, QVariantList& profiles)
{
    settings.beginGroup(SER_STREAMPROFILES);
    settings.remove(hostUuid);
    settings.beginWriteArray(hostUuid);
    for (int i = 0; i < profiles.size(); i++) {
        settings.setArrayIndex(i);

        QVariantMap profile = profiles[i].toMap();
        if (profile.value("id").toString().isEmpty()) {
            profile["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            profiles[i] = profile;
        }
        for (const QString& key : k_ProfileKeys) {
            settings.setValue(key, profile.value(key));
        }
    }
    settings.endArray();
    settings.endGroup();
}

StreamProfiles::StreamProfiles(QObject* parent)
    : QObject(parent)
{
}

QVariantList StreamProfiles::profilesForHost(const QString& hostUuid) const
{
    QSettings settings;
    QVariantList profiles;

    bool missingId = false;
    settings.beginGroup(SER_STREAMPROFILES);
    int count = settings.beginReadArray(hostUuid);
    for (int i = 0; i < count; i++) {
        settings.setArrayIndex(i);

        QVariantMap profile;
        for (const QString& key : k_ProfileKeys) {
            profile[key] = settings.value(key);
        }
        missingId = missingId || profile.value("id").toString().isEmpty();
        profiles.append(profile);
    }
    settings.endArray();
    settings.endGroup();

    // Profiles stored before they had ids get theirs once, so they stay the same from now on
    if (missingId) {
        writeProfiles(settings, hostUuid, profiles);
    }

    return profiles;
}

void StreamProfiles::setProfilesForHost(const QString& hostUuid, const QString& hostName,
                                        const QVariantList& profiles)
{
    QSettings settings;
    QVariantList stored = profiles;

    settings.setValue(QString(SER_HOSTNAMES "/%1").arg(hostUuid), hostName);
    writeProfiles(settings, hostUuid, stored);

    emit profilesChanged(hostUuid);
}

QVariantList StreamProfiles::allScreensLaunches() const
{
    QSet<QString> connected;
    for (QScreen* screen : QGuiApplication::screens()) {
        connected.insert(screen->name());
    }

    QSettings settings;
    settings.beginGroup(SER_HOSTNAMES);
    QStringList hostUuids = settings.childKeys();
    QMap<QString, QString> hostNames;
    for (const QString& hostUuid : hostUuids) {
        hostNames[hostUuid] = settings.value(hostUuid).toString();
    }
    settings.endGroup();

    QVariantList launches;
    for (const QString& hostUuid : hostUuids) {
        for (const QVariant& entry : profilesForHost(hostUuid)) {
            QVariantMap profile = entry.toMap();
            QString screen = profile.value("screen").toString();
            if (profile.value("allScreens").toBool() && !screen.isEmpty() && connected.contains(screen)) {
                QVariantMap launch;
                launch["hostUuid"] = hostUuid;
                launch["hostName"] = hostNames[hostUuid];
                launch["app"] = profile.value("app").toString();
                launch["profile"] = profile;
                launches.append(launch);
            }
        }
    }

    return launches;
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
