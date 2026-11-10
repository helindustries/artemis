#include "coherencedock.h"

#include "backend/proxylaunchers.h"

#include "SDL_compat.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// The dock tile app inside Artemis.app, which each program's tile is a copy of
#define DOCK_TILE_EXECUTABLE "ArtemisDockTile"

// About 2 points at a 64 point Dock
#define DOCK_ICON_MARGIN_PERCENT 3

// The host's program icons come this large
#define DOCK_ICON_SIZE 256

static QString bundleSafe(const QString& text)
{
    return QString(text).replace(QRegularExpression("[^A-Za-z0-9.-]"), "-");
}

CoherenceDock::CoherenceDock(NvComputer* computer, const QString& hostName)
    : m_Computer(computer),
      m_HostName(hostName),
      m_SocketPath(QDir::temp().filePath(QString("artemis-dock-%1.sock").arg(QCoreApplication::applicationPid())))
{
    struct sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    const QByteArray path = QFile::encodeName(m_SocketPath);
    unlink(path.constData());
    m_Server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_Server < 0 || path.size() >= (int)sizeof(address.sun_path) || pipe(m_Wake) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Expected a socket for the Dock icons at %s: %s", path.constData(), strerror(errno));
    }
    else {
        strcpy(address.sun_path, path.constData());
        if (bind(m_Server, (struct sockaddr*)&address, sizeof(address)) != 0 || listen(m_Server, 8) != 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Expected to listen for the Dock icons at %s: %s", path.constData(), strerror(errno));
        }
        else {
            m_ServerThread = std::thread([this]() { serve(); });
        }
    }
}

CoherenceDock::~CoherenceDock()
{
    m_Stopping = true;
    if (m_Wake[1] >= 0 && write(m_Wake[1], "x", 1) != 1) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Expected the Dock icon server to wake for the end");
    }
    if (m_ServerThread.joinable()) {
        m_ServerThread.join();
    }
    for (std::thread& starter : m_Starters) {
        starter.join();
    }
    // Every tile quits with its connection
    for (const int connection : m_Tiles) {
        if (connection >= 0) {
            close(connection);
        }
    }
    for (const int connection : m_Closing) {
        close(connection);
    }
    for (const int descriptor : { m_Server, m_Wake[0], m_Wake[1] }) {
        if (descriptor >= 0) {
            close(descriptor);
        }
    }
    unlink(QFile::encodeName(m_SocketPath).constData());
}

void CoherenceDock::update(const QVector<NvHostWindow>& windows)
{
    QHash<QString, quint64> programs { { COHERENCE_DOCK_START, 0 } };
    for (const NvHostWindow& window : windows) {
        if (!window.popup && !window.process.isEmpty() && !programs.contains(window.process)) {
            programs.insert(window.process, window.id);
        }
    }

    std::lock_guard lock(m_Lock);
    for (auto tile = m_Tiles.begin(); tile != m_Tiles.end();) {
        if (!programs.contains(tile.key())) {
            if (*tile >= 0) {
                m_Closing.append(*tile);
                if (write(m_Wake[1], "x", 1) != 1) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Expected the Dock icon server to wake for a closed tile");
                }
            }
            tile = m_Tiles.erase(tile);
        }
        else {
            ++tile;
        }
    }
    for (auto program = programs.cbegin(); program != programs.cend(); ++program) {
        if (!m_Tiles.contains(program.key()) && m_Server >= 0) {
            m_Tiles.insert(program.key(), -1);
            m_Starters.emplace_back([this, name = program.key(), id = program.value()]() { startTile(name, id); });
        }
    }
}

void CoherenceDock::startTile(const QString& program, quint64 windowId)
{
    // The icon takes seconds while the host's shell warms up; the tile is made meanwhile
    QImage icon;
    QString name;
    if (program == COHERENCE_DOCK_START) {
        icon = ProxyLaunchers::windowsLogo(DOCK_ICON_SIZE, DOCK_ICON_MARGIN_PERCENT);
        name = "Start";
    }
    else {
        try {
            icon = NvHTTP(m_Computer).getWindowIcon(windowId);
        } catch (const GfeHttpResponseException& e) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No icon for host program %s: %s", qPrintable(program), qPrintable(e.toQString()));
        } catch (const QtNetworkReplyException& e) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No icon for host program %s: %s", qPrintable(program), qPrintable(e.toQString()));
        }
        name = QFileInfo(program).completeBaseName();
        if (!name.isEmpty()) {
            name[0] = name[0].toUpper();
        }
    }
    const QString bundle = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                               .filePath(QString("DockTiles/%1/%2.app").arg(bundleSafe(m_HostName), bundleSafe(name)));
    // Made anew each time: LaunchServices keeps showing the icon a bundle at that path had
    const QString contents = bundle + "/Contents";
    QDir(bundle).removeRecursively();
    QDir().mkpath(contents + "/MacOS");
    QDir().mkpath(contents + "/Resources");

    const QString executable = contents + "/MacOS/tile";
    QFile::remove(executable);
    QFile plist(contents + "/Info.plist");
    const bool written = QFile::copy(QCoreApplication::applicationDirPath() + "/" DOCK_TILE_EXECUTABLE, executable) &&
                         plist.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                         plist.write(QString(
                             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                             "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                             "<plist version=\"1.0\"><dict>\n"
                             "  <key>CFBundleExecutable</key><string>tile</string>\n"
                             "  <key>CFBundleIdentifier</key><string>com.helindustries.artemis.docktile.%1.%2</string>\n"
                             "  <key>CFBundleName</key><string>%3</string>\n"
                             "  <key>CFBundleIconFile</key><string>icon</string>\n"
                             "  <key>CFBundlePackageType</key><string>APPL</string>\n"
                             "  <key>ArtemisProgram</key><string>%4</string>\n"
                             "  <key>ArtemisSocket</key><string>%5</string>\n"
                             "</dict></plist>\n")
                             .arg(bundleSafe(m_HostName), bundleSafe(name), name.toHtmlEscaped(), program.toHtmlEscaped(), m_SocketPath.toHtmlEscaped())
                             .toUtf8()) > 0;
    plist.close();
    // Windows icons fill their canvas to the edge, and look pressed into the Dock without a margin
    if (!icon.isNull()) {
        QImage padded(icon.size(), QImage::Format_ARGB32_Premultiplied);
        padded.fill(Qt::transparent);
        QPainter painter(&padded);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const int margin = icon.width() * DOCK_ICON_MARGIN_PERCENT / 100;
        painter.drawImage(QRect(margin, margin, icon.width() - 2 * margin, icon.height() - 2 * margin), icon);
        painter.end();
        padded.save(contents + "/Resources/icon.icns", "icns");
    }

    // -g: in the background, the program's windows keep the focus
    if (!written || !QProcess::startDetached("/usr/bin/open", { "-g", "-n", bundle })) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Expected a Dock icon for host program %s at %s", qPrintable(program), qPrintable(bundle));
    }
}

void CoherenceDock::serve()
{
    // Connections whose tile has not said which program it stands for yet
    QVector<int> pending;
    QHash<int, QByteArray> received;
    while (!m_Stopping) {
        QVector<pollfd> descriptors { { m_Server, POLLIN, 0 }, { m_Wake[0], POLLIN, 0 } };
        {
            std::lock_guard lock(m_Lock);
            for (const int connection : m_Closing) {
                received.remove(connection);
                close(connection);
            }
            m_Closing.clear();
            for (const int connection : m_Tiles) {
                if (connection >= 0) {
                    descriptors.append({ connection, POLLIN, 0 });
                }
            }
        }
        for (const int connection : pending) {
            descriptors.append({ connection, POLLIN, 0 });
        }

        if (poll(descriptors.data(), (nfds_t)descriptors.size(), -1) > 0 && !m_Stopping) {
            if (descriptors[1].revents & POLLIN) {
                char wake[16];
                if (read(m_Wake[0], wake, sizeof(wake)) < 0) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Dock icon server wake failed: %s", strerror(errno));
                }
            }
            if (descriptors[0].revents & POLLIN) {
                if (const int connection = accept(m_Server, nullptr, nullptr); connection >= 0) {
                    pending.append(connection);
                }
            }
            for (int index = 2; index < descriptors.size(); index++) {
                if (descriptors[index].revents & (POLLIN | POLLHUP)) {
                    const int connection = descriptors[index].fd;
                    char buffer[256];
                    const ssize_t length = read(connection, buffer, sizeof(buffer));
                    if (length > 0) {
                        received[connection].append(buffer, (int)length);
                    }
                    for (int end; (end = received[connection].indexOf('\n')) >= 0;) {
                        const QString line = QString::fromUtf8(received[connection].left(end));
                        received[connection].remove(0, end + 1);
                        const QString command = line.section(' ', 0, 0);
                        const QString program = line.section(' ', 1);
                        if (command == "hello") {
                            // A tile for a program that lost its windows meanwhile quits at once
                            std::lock_guard lock(m_Lock);
                            pending.removeAll(connection);
                            if (m_Tiles.contains(program) && m_Tiles.value(program) < 0) {
                                m_Tiles[program] = connection;
                            }
                            else {
                                close(connection);
                            }
                        }
                        else if (command == "raise" || command == "quit") {
                            SDL_Event event = {};
                            event.type = SDL_USEREVENT;
                            event.user.code = command == "raise" ? SDL_CODE_DOCK_RAISE : SDL_CODE_DOCK_QUIT;
                            event.user.data1 = new QString(program);
                            if (SDL_PushEvent(&event) != 1) {
                                delete static_cast<QString*>(event.user.data1);
                            }
                        }
                    }
                    if (length <= 0) {
                        // The tile quit: the program keeps its windows, and its tile comes back
                        std::lock_guard lock(m_Lock);
                        pending.removeAll(connection);
                        received.remove(connection);
                        for (auto tile = m_Tiles.begin(); tile != m_Tiles.end(); ++tile) {
                            if (*tile == connection) {
                                m_Tiles.erase(tile);
                                break;
                            }
                        }
                        close(connection);
                    }
                }
            }
        }
    }
    for (const int connection : pending) {
        close(connection);
    }
}
