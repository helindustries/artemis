#pragma once

#include <QAbstractListModel>
#include <QLocalServer>
#include <QProcess>
#include <QVariantMap>

class QLocalSocket;

// Starts each stream as its own Artemis process, so several can run at once and the
// window that started them stays usable. The streaming library keeps its connection in
// globals, so one process can only ever hold one stream.
//
// Every stream process connects back over a local socket, reports its state there and
// takes "stop" and "raise" commands from it. A stream whose process exits, for whatever
// reason, leaves the list. Quitting the window stops its streams.
class StreamProcessManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles
    {
        HostNameRole = Qt::UserRole,
        AppNameRole,
        ProfileNameRole,
        StateRole,
        DetailRole,
    };

    explicit StreamProcessManager(QObject* parent = nullptr);
    ~StreamProcessManager();

    // profile is a map as StreamProfiles stores it; an empty map streams with the
    // global settings. The host is found by hostUuid, hostName is what the list shows.
    Q_INVOKABLE void start(const QString& hostUuid, const QString& hostName, const QString& appName,
                           const QVariantMap& profile);
    Q_INVOKABLE void stop(int row);
    // Stops the streams of one host started with this profile (StreamProfiles::idOf); returns
    // how many it stopped, so a caller can fall back to quitting the host's app
    Q_INVOKABLE int stopProfile(const QString& hostUuid, const QString& profileId);
    Q_INVOKABLE void raise(int row);

    int count() const;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();
    // A stream process ended after reporting an error, which only this window can show
    void streamFailed(QString hostName, QString appName, QString detail);

private:
    struct Stream
    {
        QString id;
        QString hostUuid;
        QString profileId;
        QString hostName;
        QString appName;
        QString profileName;
        QString state;
        QString detail;
        QProcess* process;
        QLocalSocket* socket;
    };

    void handleNewConnection();
    void handleSocketData(QLocalSocket* socket);
    void handleProcessFinished(QProcess* process);
    void sendCommand(int row, const QString& command);
    int rowForProcess(const QProcess* process) const;
    int rowForId(const QString& id) const;

    QLocalServer m_Server;
    QList<Stream> m_Streams;
    int m_NextId;
};
