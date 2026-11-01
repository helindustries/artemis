#pragma once

#include <QImage>
#include <QString>
#include <QVector>

// Launchers for a host's pinned apps, kept where this platform keeps its own apps, so they
// can be pinned like local ones: macOS ~/Applications (Artemis)/<host> Applications/*.app,
// Windows' Start menu folder Artemis\<host> Applications\*.lnk, Linux .desktop entries.
// Each runs "Artemis coherence <host uuid> <app id>"; the coherence launcher leaves out the app.
namespace ProxyLaunchers
{
    struct App
    {
        QString id;
        QString name;
        QImage icon;
    };

    // Replaces the host's launchers with these and one that starts coherence mode alone;
    // launchers of apps no longer pinned are removed. Safe to call from any thread.
    void write(const QString& hostUuid, const QString& hostName, const QVector<App>& apps);

    // Windows 11's Start logo, four squares in its blue, for coherence mode's own icons
    QImage windowsLogo(int size, int marginPercent);
}
