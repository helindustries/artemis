#include "streamprocessmanager.h"
#include "settings/streamprofiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QRect>
#include <QTimer>

#include <algorithm>

// How long a stream process gets to end its session after "stop" before it is killed
#define STOP_GRACE_MS 10000
// Relative to the main executable's directory, Contents/MacOS
#define STREAM_HELPER_EXECUTABLE "../Helpers/Artemis Stream.app/Contents/MacOS/Artemis"

StreamProcessManager::StreamProcessManager(QObject* parent)
    : QAbstractListModel(parent),
      m_NextId(1)
{
    QString name = QString("artemis-streams-%1").arg(QCoreApplication::applicationPid());
    QLocalServer::removeServer(name);
    if (!m_Server.listen(name)) {
        qWarning() << "Expected to listen on stream control socket" << name << "but:" << m_Server.errorString()
                   << "- streams will start, but cannot be stopped or tracked from this window";
    }
    connect(&m_Server, &QLocalServer::newConnection, this, &StreamProcessManager::handleNewConnection);
}

StreamProcessManager::~StreamProcessManager()
{
    // A QProcess kills its process when destroyed, so the streams end with this window:
    // they are asked to stop cleanly first
    for (int row = 0; row < m_Streams.size(); row++) {
        m_Streams[row].process->disconnect(this);
        sendCommand(row, "stop");
    }
    for (Stream& stream : m_Streams) {
        if (!stream.process->waitForFinished(STOP_GRACE_MS)) {
            stream.process->kill();
            stream.process->waitForFinished();
        }
    }
}

QString StreamProcessManager::start(const QString& hostUuid, const QString& hostName, const QString& appName,
                                    const QVariantMap& profile)
{
    // An app streams once per profile: starting it again, as its tile's play button does while it
    // runs, brings its window to the front instead of a second stream of the same screen
    if (const int running = runningRow(hostUuid, appName, profile); running >= 0) {
        raise(running);
        return m_Streams[running].id;
    }

    Stream stream;
    stream.id = QString::number(m_NextId++);
    stream.hostUuid = hostUuid;
    stream.profileId = StreamProfiles::idOf(profile);
    stream.hostName = hostName;
    stream.appName = appName;
    stream.profileName = profile.value("name").toString();
    stream.state = "starting";
    stream.socket = nullptr;
    stream.reportsProgress = profile.value("windowId").toULongLong() == 0;
    stream.process = new QProcess(this);

    QStringList arguments = { "stream", hostUuid.isEmpty() ? hostName : hostUuid, appName,
                              "--control-socket", m_Server.fullServerName(),
                              "--stream-id", stream.id,
                              "--profile-id", StreamProfiles::idOf(profile) };
    if (profile.value("width").toInt() > 0 && profile.value("height").toInt() > 0) {
        arguments << "--resolution" << QString("%1x%2").arg(profile.value("width").toInt())
                                                        .arg(profile.value("height").toInt());
    }
    if (profile.value("fps").toInt() > 0) {
        arguments << "--fps" << QString::number(profile.value("fps").toInt());
    }
    if (profile.value("bitrateKbps").toInt() > 0) {
        arguments << "--bitrate" << QString::number(profile.value("bitrateKbps").toInt());
    }
    if (!profile.value("displayMode").toString().isEmpty()) {
        arguments << "--display-mode" << profile.value("displayMode").toString();
    }
    if (!profile.value("screen").toString().isEmpty()) {
        arguments << "--screen" << profile.value("screen").toString();
    }
    if (profile.value("windowId").toULongLong() != 0) {
        arguments << "--window-id" << QString::number(profile.value("windowId").toULongLong())
                  << "--window-title" << profile.value("windowTitle").toString()
                  << "--window-position" << QString("%1,%2").arg(profile.value("windowX").toInt())
                                                            .arg(profile.value("windowY").toInt());
        if (const QRect area = profile.value("coherenceArea").toRect(); area.isValid()) {
            arguments << "--coherence-area" << QString("%1,%2,%3,%4").arg(area.x()).arg(area.y()).arg(area.width()).arg(area.height());
        }
    }
    else if (profile.value("coherenceAtlas").toBool()) {
        const QRect area = profile.value("coherenceArea").toRect();
        arguments << "--coherence-atlas"
                  << "--coherence-area" << QString("%1,%2,%3,%4").arg(area.x()).arg(area.y()).arg(area.width()).arg(area.height());
    }
    if (profile.value("absoluteMouse").toBool()) {
        arguments << "--absolute-mouse";
    }

    // Portable mode is chosen by the working directory, so the stream must share ours
    stream.process->setWorkingDirectory(QDir::currentPath());
    stream.process->setProcessChannelMode(QProcess::ForwardedChannels);
    connect(stream.process, &QProcess::finished, this, [this, process = stream.process]() {
        handleProcessFinished(process);
    });
    const QString executable = streamExecutable();
    connect(stream.process, &QProcess::errorOccurred, this, [this, process = stream.process, executable](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            qWarning() << "Expected to start stream process" << executable << "but:" << process->errorString();
            handleProcessFinished(process);
        }
    });

    beginInsertRows(QModelIndex(), m_Streams.size(), m_Streams.size());
    m_Streams.append(stream);
    endInsertRows();
    emit countChanged();

    stream.process->start(executable, arguments);
    if (stream.reportsProgress) {
        emit streamStateChanged(stream.id, stream.hostName, stream.appName, stream.state, QString());
    }
    return stream.id;
}

QString StreamProcessManager::streamExecutable()
{
    QString executable = QCoreApplication::applicationFilePath();
#ifdef Q_OS_DARWIN
    // The bundle's agent app (app.pro), whose processes stay out of the Dock
    if (const QString helper = QCoreApplication::applicationDirPath() + "/" + STREAM_HELPER_EXECUTABLE;
            QFileInfo::exists(helper)) {
        executable = helper;
    }
    else {
        qWarning() << "Expected the stream helper at" << helper << "- streams will show in the Dock";
    }
#endif
    return executable;
}

void StreamProcessManager::quitHostApp(const QString& id)
{
    if (const int row = rowForId(id); row >= 0) {
        sendCommand(row, "quitapp");
    }
}

int StreamProcessManager::runningRow(const QString& hostUuid, const QString& appName, const QVariantMap& profile) const
{
    // Coherence mode's window and atlas streams are several of one app by design
    int found = -1;
    if (profile.value("windowId").toULongLong() == 0 && !profile.value("coherenceAtlas").toBool()) {
        const QString profileId = StreamProfiles::idOf(profile);
        for (int row = 0; row < m_Streams.size() && found < 0; row++) {
            if (m_Streams[row].hostUuid == hostUuid && m_Streams[row].appName == appName && m_Streams[row].profileId == profileId) {
                found = row;
            }
        }
    }
    return found;
}

void StreamProcessManager::stopStream(const QString& id)
{
    stop(rowForId(id));
}

int StreamProcessManager::stopProfile(const QString& hostUuid, const QString& profileId)
{
    int stopped = 0;
    for (int row = 0; row < m_Streams.size(); row++) {
        if (m_Streams[row].hostUuid == hostUuid && m_Streams[row].profileId == profileId) {
            stop(row);
            stopped++;
        }
    }
    return stopped;
}

bool StreamProcessManager::streamsTo(const QString& hostUuid) const
{
    return std::any_of(m_Streams.cbegin(), m_Streams.cend(),
                       [&hostUuid](const Stream& stream) { return stream.hostUuid == hostUuid; });
}

void StreamProcessManager::commandStream(const QString& id, const QJsonObject& message)
{
    const int row = rowForId(id);
    if (row >= 0) {
        QLocalSocket* socket = m_Streams[row].socket;
        if (socket != nullptr && socket->state() == QLocalSocket::ConnectedState) {
            socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
        }
    }
}

void StreamProcessManager::stop(int row)
{
    if (row >= 0 && row < m_Streams.size()) {
        QProcess* process = m_Streams[row].process;
        sendCommand(row, "stop");
        QTimer::singleShot(STOP_GRACE_MS, process, [process]() {
            if (process->state() != QProcess::NotRunning) {
                qWarning() << "Stream process" << process->processId() << "did not end within"
                           << STOP_GRACE_MS << "ms of stop, killing it";
                process->kill();
            }
        });
    }
}

void StreamProcessManager::raise(int row)
{
    if (row >= 0 && row < m_Streams.size()) {
        sendCommand(row, "raise");
    }
}

void StreamProcessManager::sendCommand(int row, const QString& command)
{
    QLocalSocket* socket = m_Streams[row].socket;
    if (socket != nullptr && socket->state() == QLocalSocket::ConnectedState) {
        QJsonObject message;
        message["command"] = command;
        socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    }
    else if (command == "stop") {
        // Not connected yet, or no longer: nothing to end cleanly
        m_Streams[row].process->terminate();
    }
}

void StreamProcessManager::handleNewConnection()
{
    while (QLocalSocket* socket = m_Server.nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { handleSocketData(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            for (Stream& stream : m_Streams) {
                if (stream.socket == socket) {
                    stream.socket = nullptr;
                }
            }
            socket->deleteLater();
        });
    }
}

void StreamProcessManager::handleSocketData(QLocalSocket* socket)
{
    QByteArray buffer = socket->property("buffer").toByteArray() + socket->readAll();

    int newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        QJsonObject message = QJsonDocument::fromJson(buffer.left(newline)).object();
        buffer.remove(0, newline + 1);

        int row = rowForId(message["id"].toString());
        if (row >= 0 && message.contains("event")) {
            m_Streams[row].socket = socket;
            emit streamEvent(m_Streams[row].id, message["event"].toString(), message["detail"].toString());
        }
        else if (row >= 0) {
            Stream& stream = m_Streams[row];
            stream.socket = socket;
            stream.state = message["state"].toString();
            stream.detail = message["detail"].toString();
            emit dataChanged(index(row), index(row), { StateRole, DetailRole });
            if (stream.reportsProgress) {
                emit streamStateChanged(stream.id, stream.hostName, stream.appName, stream.state, stream.detail);
            }
        }
        else {
            qWarning() << "Stream control message for unknown stream" << message["id"].toString();
        }
    }

    socket->setProperty("buffer", buffer);
}

void StreamProcessManager::handleProcessFinished(QProcess* process)
{
    int row = rowForProcess(process);
    if (row >= 0) {
        if (const Stream& stream = m_Streams[row]; stream.state == "failed") {
            emit streamFailed(stream.hostName, stream.appName, stream.detail);
        }
        emit streamEnded(m_Streams[row].id);
        beginRemoveRows(QModelIndex(), row, row);
        m_Streams.removeAt(row);
        endRemoveRows();
        emit countChanged();
    }
    process->deleteLater();
}

int StreamProcessManager::rowForProcess(const QProcess* process) const
{
    int row = -1;
    for (int i = 0; i < m_Streams.size() && row < 0; i++) {
        if (m_Streams[i].process == process) {
            row = i;
        }
    }
    return row;
}

int StreamProcessManager::rowForId(const QString& id) const
{
    int row = -1;
    for (int i = 0; i < m_Streams.size() && row < 0; i++) {
        if (m_Streams[i].id == id) {
            row = i;
        }
    }
    return row;
}

int StreamProcessManager::count() const
{
    return m_Streams.size();
}

int StreamProcessManager::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_Streams.size();
}

QVariant StreamProcessManager::data(const QModelIndex& index, int role) const
{
    QVariant value;

    if (index.isValid() && index.row() < m_Streams.size()) {
        const Stream& stream = m_Streams[index.row()];
        switch (role) {
        case HostNameRole:
            value = stream.hostName;
            break;
        case AppNameRole:
            value = stream.appName;
            break;
        case ProfileNameRole:
            value = stream.profileName;
            break;
        case StateRole:
            value = stream.state;
            break;
        case DetailRole:
            value = stream.detail;
            break;
        }
    }

    return value;
}

QHash<int, QByteArray> StreamProcessManager::roleNames() const
{
    return {
        { HostNameRole, "hostName" },
        { AppNameRole, "appName" },
        { ProfileNameRole, "profileName" },
        { StateRole, "state" },
        { DetailRole, "detail" },
    };
}
