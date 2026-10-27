// Test of StreamProfiles' storage and the "Stream on all screens" selection. It opens no
// window but needs a real screen: the offscreen platform's screen has no name, and an
// empty name is what an unpinned profile stores.

#include "settings/streamprofiles.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

static int s_Failures = 0;

static void expect(bool condition, const QString& message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", qPrintable(message));
        s_Failures++;
    }
}

static QVariantMap profile(const QString& name, const QString& screen, bool allScreens)
{
    return { { "name", name }, { "width", 1728 }, { "height", 1116 }, { "fps", 120 },
             { "bitrateKbps", 27000 }, { "displayMode", "borderless" }, { "screen", screen },
             { "app", "Virtual Display" }, { "allScreens", allScreens } };
}

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    if (QGuiApplication::primaryScreen() == nullptr || QGuiApplication::primaryScreen()->name().isEmpty()) {
        fprintf(stderr, "expected a connected screen with a name to pin profiles to\n");
        return 2;
    }

    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName("artemis-test");
    QCoreApplication::setApplicationName("streamprofiles");

    QString screen = QGuiApplication::primaryScreen()->name();
    StreamProfiles profiles;

    profiles.setProfilesForHost("HOST-1", "Heimdall",
                                { profile("Retina", screen, true), profile("Plain", screen, false) });
    profiles.setProfilesForHost("HOST-2", "Heimdall-2",
                                { profile("Ultrawide", "Not Connected", true), profile("Anywhere", "", true) });

    // What is stored comes back as it went in
    QVariantList stored = profiles.profilesForHost("HOST-1");
    expect(stored.size() == 2, QString("expected 2 profiles for HOST-1, got %1").arg(stored.size()));
    expect(stored.value(0).toMap().value("name") == "Retina", "expected the profile order to be kept");
    expect(stored.value(0).toMap().value("app") == "Virtual Display", "expected the app to be stored");
    expect(stored.value(0).toMap().value("allScreens").toBool(), "expected allScreens to be stored");

    // Only a pinned, connected, allScreens profile is started with all screens
    QVariantList launches = profiles.allScreensLaunches();
    expect(launches.size() == 1, QString("expected exactly one launch, got %1").arg(launches.size()));
    QVariantMap launch = launches.value(0).toMap();
    expect(launch.value("hostUuid") == "HOST-1", "expected the launch on HOST-1");
    expect(launch.value("hostName") == "Heimdall", "expected the host's name with the launch");
    expect(launch.value("app") == "Virtual Display", "expected the profile's app in the launch");
    expect(launch.value("profile").toMap().value("name") == "Retina", "expected the Retina profile");

    // Replacing a host's profiles drops the old ones
    profiles.setProfilesForHost("HOST-1", "Heimdall", { profile("Plain", screen, false) });
    expect(profiles.profilesForHost("HOST-1").size() == 1, "expected the old profiles to be replaced");
    expect(profiles.allScreensLaunches().isEmpty(), "expected no launches once no profile qualifies");

    printf("%s\n", s_Failures == 0 ? "all checks passed" : "checks failed");
    return s_Failures == 0 ? 0 : 1;
}
