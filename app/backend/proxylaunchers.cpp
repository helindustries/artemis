#include "proxylaunchers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QImageWriter>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#ifdef Q_OS_WIN32
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace
{

// The host's app and host names can hold characters macOS, Windows or a shell trips over
QString fileName(const QString& name)
{
    QString cleaned = name;
    cleaned.replace(QRegularExpression(R"([\\/:*?"<>|\x00-\x1f])"), "-");
    cleaned = cleaned.trimmed();
    return cleaned.isEmpty() ? QStringLiteral("App") : cleaned;
}

QStringList command(const QString& hostUuid, const QString& appId)
{
    return { QCoreApplication::applicationFilePath(), "coherence", hostUuid, appId };
}

bool writeFile(const QString& path, const QByteArray& content)
{
    QFile file(path);
    const bool written = file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(content) == content.size();
    if (!written) {
        qWarning() << "Expected to write launcher file" << path << "but:" << file.errorString();
    }
    return written;
}

void writeIcon(const QImage& icon, const QString& path, const char* format)
{
    if (!icon.isNull()) {
        QImageWriter writer(path, format);
        if (!writer.write(icon)) {
            qWarning() << "Expected to write launcher icon" << path << "but:" << writer.errorString();
        }
    }
}

// What an earlier write made for apps that are no longer pinned; the folder is ours alone
void removeStale(const QDir& directory, const QStringList& nameFilters, const QSet<QString>& current)
{
    for (const QFileInfo& entry : directory.entryInfoList(nameFilters, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!current.contains(entry.fileName())) {
            if (entry.isDir()) {
                QDir(entry.filePath()).removeRecursively();
            }
            else {
                QFile::remove(entry.filePath());
            }
        }
    }
}

#if defined(Q_OS_DARWIN)

QString shellQuoted(const QString& text)
{
    QString quoted = text;
    quoted.replace("'", "'\\''");
    return "'" + quoted + "'";
}

void writeHost(const QString& hostUuid, const QString& hostName, const QVector<ProxyLaunchers::App>& apps)
{
    QDir directory(QDir::home().filePath(QString("Applications (Artemis)/%1 Applications").arg(fileName(hostName))));
    directory.mkpath(".");

    QSet<QString> current;
    for (const ProxyLaunchers::App& app : apps) {
        const QString bundleName = fileName(app.name) + ".app";
        const QString contents = directory.filePath(bundleName + "/Contents");
        current.insert(bundleName);
        QDir().mkpath(contents + "/MacOS");
        QDir().mkpath(contents + "/Resources");

        // LSUIElement: the launcher hands its app over and exits, it must not bounce in the
        // Dock next to its pinned icon
        const QString bundleId = QCryptographicHash::hash((hostUuid + app.id).toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
        writeFile(contents + "/Info.plist", QString(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\"><dict>\n"
            "  <key>CFBundleExecutable</key><string>launcher</string>\n"
            "  <key>CFBundleIdentifier</key><string>com.helindustries.artemis.proxy.%1</string>\n"
            "  <key>CFBundleName</key><string>%2</string>\n"
            "  <key>CFBundleIconFile</key><string>icon</string>\n"
            "  <key>CFBundlePackageType</key><string>APPL</string>\n"
            "  <key>LSUIElement</key><true/>\n"
            "</dict></plist>\n").arg(bundleId, app.name.toHtmlEscaped()).toUtf8());

        QStringList quoted;
        for (const QString& part : command(hostUuid, app.id)) {
            quoted.append(shellQuoted(part));
        }
        const QString launcher = contents + "/MacOS/launcher";
        if (writeFile(launcher, ("#!/bin/sh\nexec " + quoted.join(' ') + "\n").toUtf8())) {
            QFile::setPermissions(launcher, QFile::permissions(launcher) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
        }
        writeIcon(app.icon, contents + "/Resources/icon.icns", "icns");
    }
    removeStale(directory, { "*.app" }, current);
}

#elif defined(Q_OS_WIN32)

std::wstring nativePath(const QString& path)
{
    return QDir::toNativeSeparators(path).toStdWString();
}

void writeHost(const QString& hostUuid, const QString& hostName, const QVector<ProxyLaunchers::App>& apps)
{
    // ApplicationsLocation is the user's Start Menu\Programs; icons stay out of it, where Start would list them
    QDir directory(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) +
                   QString("/Artemis/%1 Applications").arg(fileName(hostName)));
    QDir icons(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/proxy-icons/" + hostUuid);
    directory.mkpath(".");
    icons.mkpath(".");

    // The shell link is COM, and the pool thread this runs on has no apartment of its own
    const bool comInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    QSet<QString> current;
    QSet<QString> currentIcons;
    for (const ProxyLaunchers::App& app : apps) {
        const QString shortcutName = fileName(app.name) + ".lnk";
        const QString iconName = fileName(app.name) + ".ico";
        current.insert(shortcutName);
        currentIcons.insert(iconName);
        writeIcon(app.icon.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation), icons.filePath(iconName), "ico");

        // Ids are the host's shortcut file names, which cannot hold quotes
        const QStringList parts = command(hostUuid, app.id);
        QStringList arguments;
        for (const QString& part : parts.mid(1)) {
            arguments.append('"' + part + '"');
        }

        IShellLinkW* link = nullptr;
        HRESULT status = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
        if (SUCCEEDED(status)) {
            link->SetPath(nativePath(parts.first()).c_str());
            link->SetArguments(arguments.join(' ').toStdWString().c_str());
            link->SetIconLocation(nativePath(icons.filePath(iconName)).c_str(), 0);
            link->SetDescription(QString("%1 on %2").arg(app.name, hostName).toStdWString().c_str());

            IPersistFile* file = nullptr;
            status = link->QueryInterface(IID_PPV_ARGS(&file));
            if (SUCCEEDED(status)) {
                status = file->Save(nativePath(directory.filePath(shortcutName)).c_str(), TRUE);
                file->Release();
            }
            link->Release();
        }
        if (FAILED(status)) {
            qWarning() << "Expected to write launcher" << directory.filePath(shortcutName)
                       << "but got" << QString::number(static_cast<quint32>(status), 16);
        }
    }
    if (comInitialized) {
        CoUninitialize();
    }
    removeStale(directory, { "*.lnk" }, current);
    removeStale(icons, { "*.ico" }, currentIcons);
}

#else

// Exec arguments are quoted, and the quoting backslashes are themselves escaped in the value
QString desktopQuoted(const QString& text)
{
    QString quoted = text;
    quoted.replace("\\", "\\\\\\\\").replace("\"", "\\\\\"").replace("`", "\\\\`").replace("$", "\\\\$");
    return "\"" + quoted + "\"";
}

void writeHost(const QString& hostUuid, const QString& hostName, const QVector<ProxyLaunchers::App>& apps)
{
    QDir directory(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) + "/artemis-" + hostUuid);
    directory.mkpath(".");

    QSet<QString> current;
    for (const ProxyLaunchers::App& app : apps) {
        // Desktop file ids allow few characters, so the name goes inside, not into the file name
        const QString base = "app-" + QCryptographicHash::hash(app.id.toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
        current.insert(base + ".desktop");
        current.insert(base + ".png");
        writeIcon(app.icon, directory.filePath(base + ".png"), "png");

        QStringList quoted;
        for (const QString& part : command(hostUuid, app.id)) {
            quoted.append(desktopQuoted(part));
        }
        writeFile(directory.filePath(base + ".desktop"), QString(
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=%1\n"
            "Comment=%1 on %2, in Artemis coherence mode\n"
            "Exec=%3\n"
            "Icon=%4\n"
            "Terminal=false\n"
            "Categories=Network;RemoteAccess;\n").arg(app.name, hostName, quoted.join(' '), directory.filePath(base + ".png")).toUtf8());
    }
    removeStale(directory, { "*.desktop", "*.png" }, current);
}

#endif

}

void ProxyLaunchers::write(const QString& hostUuid, const QString& hostName, const QVector<App>& apps)
{
    writeHost(hostUuid, hostName, apps);
}
