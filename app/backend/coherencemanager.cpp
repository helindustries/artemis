#include "coherencemanager.h"
#include "computermanager.h"
#include "nvcomputer.h"
#include "proxylaunchers.h"
#include "streamprocessmanager.h"
#include "settings/streamingpreferences.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QReadLocker>
#include <QThreadPool>

// How often the host's window list is read while coherence mode runs
#define POLL_INTERVAL_MS 2000

// How long a host that was just woken or not yet found may take to answer the first time
#define REACH_GRACE_MS 30000


// How long a proxy launcher waits for a running Artemis window to take its app
#define HAND_OFF_TIMEOUT_MS 1000

// Windows smaller than this are tooltips and splash fragments, not worth a stream of their own
#define MIN_WINDOW_SIZE 64

static QString handOffServerName()
{
    // Unix sockets live in the user's temp directory, Windows pipes are global: name the user
    return QString("artemis-coherence-%1").arg(qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME")));
}

CoherenceManager::CoherenceManager(ComputerManager* computerManager, StreamProcessManager* streams, QObject* parent)
    : QObject(parent),
      m_ComputerManager(computerManager),
      m_Streams(streams)
{
    m_Timer.setInterval(POLL_INTERVAL_MS);
    connect(&m_Timer, &QTimer::timeout, this, &CoherenceManager::poll);
    connect(m_Streams, &StreamProcessManager::streamEnded, this, &CoherenceManager::handleStreamEnded);
    connect(m_Streams, &StreamProcessManager::streamEvent, this, &CoherenceManager::handleStreamEvent);

    // A socket left by a crashed Artemis blocks the name; one that still answers is a
    // running window, which keeps it
    connect(&m_HandOffServer, &QLocalServer::newConnection, this, &CoherenceManager::handleHandOff);
    if (!m_HandOffServer.listen(handOffServerName())) {
        QLocalSocket probe;
        probe.connectToServer(handOffServerName());
        if (!probe.waitForConnected(HAND_OFF_TIMEOUT_MS)) {
            QLocalServer::removeServer(handOffServerName());
            m_HandOffServer.listen(handOffServerName());
        }
    }
    if (!m_HandOffServer.isListening()) {
        qWarning() << "Another Artemis window takes the pinned apps' launches, this one does not:"
                   << m_HandOffServer.errorString();
    }
}

bool CoherenceManager::handOff(const QString& hostUuid, const QString& appId)
{
    QLocalSocket socket;
    socket.connectToServer(handOffServerName());
    bool handedOff = socket.waitForConnected(HAND_OFF_TIMEOUT_MS);
    if (handedOff) {
        QJsonObject message;
        message["host"] = hostUuid;
        message["app"] = appId;
        socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
        handedOff = socket.waitForBytesWritten(HAND_OFF_TIMEOUT_MS);
        socket.disconnectFromServer();
    }
    return handedOff;
}

void CoherenceManager::handleHandOff()
{
    while (QLocalSocket* socket = m_HandOffServer.nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            while (socket->canReadLine()) {
                const QJsonObject message = QJsonDocument::fromJson(socket->readLine()).object();
                launchApp(message["host"].toString(), message["app"].toString());
            }
        });
    }
}

QString CoherenceManager::hostName() const
{
    return m_Computer != nullptr ? m_Computer->name : QString();
}

void CoherenceManager::start(const QString& hostUuid)
{
    stop();

    for (NvComputer* computer : m_ComputerManager->getComputers()) {
        if (computer->uuid == hostUuid) {
            m_Computer = computer;
        }
    }

    if (m_Computer != nullptr) {
        m_HostUuid = hostUuid;
        m_Reached = false;
        m_FailedPolls = 0;
        m_ReachDeadline.setRemainingTime(REACH_GRACE_MS);
        emit hostUuidChanged();
        m_Timer.start();
        poll();
    }
    else {
        qWarning() << "Expected a known host for coherence mode, got" << hostUuid;
    }
}

void CoherenceManager::stop()
{
    m_Timer.stop();
    for (const WindowStream& stream : m_StreamOfWindow) {
        m_Streams->stopStream(stream.streamId);
    }
    m_StreamOfWindow.clear();
    m_ListedWindows.clear();
    m_DismissedWindows.clear();
    m_PendingApp.clear();
    m_Computer = nullptr;
    if (!m_HostUuid.isEmpty()) {
        m_HostUuid.clear();
        emit hostUuidChanged();
    }
}

void CoherenceManager::launchApp(const QString& hostUuid, const QString& appId)
{
    if (hostUuid != m_HostUuid) {
        start(hostUuid);
    }
    if (m_Computer != nullptr) {
        // Sent once the host has answered, so a host still being found gets it too
        m_PendingApp = appId;
        if (m_Reached) {
            poll();
        }
    }
}

void CoherenceManager::openStartMenu()
{
    // Any of the host's window streams will do: the key goes to the host, not to its window
    if (m_StreamOfWindow.isEmpty()) {
        qWarning() << "Expected a window stream of" << hostName() << "to send the start menu key through, there is none";
    }
    else {
        m_Streams->commandStream(m_StreamOfWindow.cbegin()->streamId, { { "command", "startmenu" } });
    }
}

void CoherenceManager::requestHost(const QString& what, std::function<void(NvHTTP&)> request)
{
    if (m_Computer != nullptr) {
        QThreadPool::globalInstance()->start([computer = m_Computer, what, request]() {
            try {
                NvHTTP http(computer);
                request(http);
            } catch (const GfeHttpResponseException& e) {
                qWarning() << "Expected the coherence host to" << what << "but:" << e.toQString();
            } catch (const QtNetworkReplyException& e) {
                qWarning() << "Expected the coherence host to" << what << "but:" << e.toQString();
            }
        });
    }
}

void CoherenceManager::poll()
{
    // One request at a time: a slow host must not pile them up
    if (!m_Polling && m_Computer != nullptr) {
        m_Polling = true;
        QThreadPool::globalInstance()->start([this, computer = m_Computer, hostUuid = m_HostUuid]() {
            try {
                NvHTTP http(computer);
                const QVector<NvHostWindow> windows = http.getWindows();
                QMetaObject::invokeMethod(this, [this, hostUuid, windows]() { handleWindows(hostUuid, windows); },
                                          Qt::QueuedConnection);
            } catch (const GfeHttpResponseException& e) {
                QMetaObject::invokeMethod(this, [this, hostUuid, detail = e.toQString()]() { handleError(hostUuid, detail); },
                                          Qt::QueuedConnection);
            } catch (const QtNetworkReplyException& e) {
                QMetaObject::invokeMethod(this, [this, hostUuid, detail = e.toQString()]() { handleError(hostUuid, detail); },
                                          Qt::QueuedConnection);
            }
        });
    }
}

void CoherenceManager::handleWindows(const QString& hostUuid, const QVector<NvHostWindow>& windows)
{
    m_Polling = false;

    // A reply from before a stop or a switch to another host
    if (hostUuid == m_HostUuid) {
        m_FailedPolls = 0;
        if (!m_Reached) {
            m_Reached = true;
            refreshLaunchers();
        }
        if (!m_PendingApp.isEmpty()) {
            requestHost("launch pinned app " + m_PendingApp,
                        [appId = m_PendingApp](NvHTTP& http) { http.launchPinnedApp(appId); });
            m_PendingApp.clear();
        }

        m_ListedWindows.clear();
        for (const NvHostWindow& window : windows) {
            m_ListedWindows.insert(window.id);
        }

        for (auto iter = m_StreamOfWindow.begin(); iter != m_StreamOfWindow.end();) {
            if (!m_ListedWindows.contains(iter.key())) {
                m_Streams->stopStream(iter->streamId);
                iter = m_StreamOfWindow.erase(iter);
            }
            else {
                ++iter;
            }
        }
        m_DismissedWindows.intersect(m_ListedWindows);

        for (const NvHostWindow& window : windows) {
            if (auto tracked = m_StreamOfWindow.find(window.id); tracked != m_StreamOfWindow.end()) {
                followWindow(*tracked, window);
            }
            // A minimized window has nothing to capture until it is restored
            else if (!m_DismissedWindows.contains(window.id) && !window.minimized &&
                     window.width >= MIN_WINDOW_SIZE && window.height >= MIN_WINDOW_SIZE) {
                m_StreamOfWindow.insert(window.id, { startStream(window), window });
            }
        }
    }
}

void CoherenceManager::handleError(const QString& hostUuid, const QString& detail)
{
    m_Polling = false;

    if (hostUuid == m_HostUuid) {
        // The host serves requests one at a time and answers late while it launches the
        // streams of new windows, or on a mobile link: the windows streaming keep streaming
        if (m_Reached) {
            if (m_FailedPolls++ == 0) {
                qWarning() << "Window list of" << hostName() << "unavailable, keeping its streams:" << detail;
            }
        }
        // A host found only after start (woken, or not polled yet) gets its grace period
        else if (m_ReachDeadline.hasExpired()) {
            const QString name = hostName();
            qWarning() << "Expected the window list of" << name << "for coherence mode, but:" << detail;
            stop();
            emit failed(name, detail);
        }
    }
}

void CoherenceManager::handleStreamEnded(const QString& streamId)
{
    // A window whose stream was stopped because it went is no longer here; one still listed
    // was closed by the user
    for (auto iter = m_StreamOfWindow.begin(); iter != m_StreamOfWindow.end(); ++iter) {
        if (iter->streamId == streamId) {
            if (m_ListedWindows.contains(iter.key())) {
                m_DismissedWindows.insert(iter.key());
            }
            m_StreamOfWindow.erase(iter);
            break;
        }
    }
}

void CoherenceManager::followWindow(WindowStream& stream, const NvHostWindow& window)
{
    const NvHostWindow last = stream.window;
    stream.window = window;

    if (window.minimized != last.minimized) {
        m_Streams->commandStream(stream.streamId, { { "command", window.minimized ? "minimize" : "restore" } });
    }
    // A minimized window's placement is its restored one, which is no move to follow
    else if (!window.minimized && (window.width != last.width || window.height != last.height)) {
        // A stream's resolution is fixed; the old stream's end is not a dismissal, it is no
        // longer this window's
        const QString oldStream = stream.streamId;
        stream.streamId = startStream(window);
        m_Streams->stopStream(oldStream);
    }
    else if (!window.minimized && (window.x != last.x || window.y != last.y)) {
        m_Streams->commandStream(stream.streamId, { { "command", "move" },
                                                    { "dx", window.x - last.x },
                                                    { "dy", window.y - last.y } });
    }
}

void CoherenceManager::handleStreamEvent(const QString& streamId, const QString& event, const QString& detail)
{
    for (auto iter = m_StreamOfWindow.cbegin(); iter != m_StreamOfWindow.cend(); ++iter) {
        if (iter->streamId == streamId) {
            const quint64 windowId = iter.key();
            if (event == "resized") {
                // The host window takes the size; the next poll sees it and restarts the stream
                const QStringList size = detail.split('x');
                const int width = size.value(0).toInt();
                const int height = size.value(1).toInt();
                requestHost("resize window " + QString::number(windowId),
                            [windowId, width, height](NvHTTP& http) { http.resizeWindow(windowId, width, height); });
            }
            else if (event == "restored") {
                requestHost("restore window " + QString::number(windowId),
                            [windowId](NvHTTP& http) { http.raiseWindow(windowId); });
            }
            break;
        }
    }
}

void CoherenceManager::refreshLaunchers()
{
    requestHost("list its pinned apps", [hostUuid = m_HostUuid, hostName = hostName()](NvHTTP& http) {
        QVector<ProxyLaunchers::App> apps;
        for (const NvPinnedApp& app : http.getPinnedApps()) {
            apps.append({ app.id, app.name, http.getPinnedAppIcon(app.id) });
        }
        ProxyLaunchers::write(hostUuid, hostName, apps);
    });
}

QString CoherenceManager::startStream(const NvHostWindow& window)
{
    QString hostName;
    QString appName = "Desktop";
    {
        // The stream joins whatever runs on the host instead of asking to quit it
        QReadLocker lock(&m_Computer->lock);
        hostName = m_Computer->name;
        for (const NvApp& app : m_Computer->appList) {
            if (m_Computer->currentGameId != 0 && app.id == m_Computer->currentGameId) {
                appName = app.name;
            }
        }
    }

    const int width = window.width & ~1;
    const int height = window.height & ~1;
    const int fps = StreamingPreferences::get()->fps;
    const QVariantMap profile {
        { "width", width },
        { "height", height },
        { "fps", fps },
        { "bitrateKbps", StreamingPreferences::getDefaultBitrate(width, height, fps, false) },
        { "displayMode", "windowed" },
        { "windowId", QVariant::fromValue<qulonglong>(window.id) },
        { "windowTitle", window.title },
        // Where the host has it: windows keep their arrangement, one host pixel per point
        { "windowX", window.x },
        { "windowY", window.y },
        { "absoluteMouse", true },
    };
    return m_Streams->start(m_HostUuid, hostName, appName, profile);
}
