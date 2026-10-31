#pragma once

#include "nvhttp.h"

#include <QDeadlineTimer>
#include <QHash>
#include <QLocalServer>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVector>

#include <functional>

class ComputerManager;
class NvComputer;
class StreamProcessManager;

// Coherence mode: every window of one host as a window of its own here. The host's window
// list is polled, a stream process is started for each window and stopped when the window
// goes. A stream the user closes stays closed while its window lives, so it does not come
// straight back.
//
// The host's pinned apps get launchers here (ProxyLaunchers). They run "Artemis coherence
// <host> <app>", which hands the app over this window's local socket, so only one Artemis
// window ever runs coherence mode.
class CoherenceManager : public QObject
{
    Q_OBJECT
    // The host in coherence mode, empty when none is
    Q_PROPERTY(QString hostUuid READ hostUuid NOTIFY hostUuidChanged)
    Q_PROPERTY(QString hostName READ hostName NOTIFY hostUuidChanged)

public:
    CoherenceManager(ComputerManager* computerManager, StreamProcessManager* streams, QObject* parent = nullptr);

    Q_INVOKABLE void start(const QString& hostUuid);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void openStartMenu();

    // Starts coherence mode on the host if it does not run yet, then the pinned app
    void launchApp(const QString& hostUuid, const QString& appId);

    // Gives a running Artemis window the app to launch; false when none listens
    static bool handOff(const QString& hostUuid, const QString& appId);

    QString hostUuid() const { return m_HostUuid; }
    QString hostName() const;

signals:
    void hostUuidChanged();
    // Coherence mode ended because the host's window list could not be read
    void failed(QString hostName, QString detail);

private:
    void poll();
    void handleWindows(const QString& hostUuid, const QVector<NvHostWindow>& windows);
    void handleError(const QString& hostUuid, const QString& detail);
    void handleStreamEnded(const QString& streamId);
    void handleStreamEvent(const QString& streamId, const QString& event, const QString& detail);
    void handleHandOff();
    QString startStream(const NvHostWindow& window);
    void refreshLaunchers();
    // Runs a request to the coherence host on a pool thread; failures are logged with what
    void requestHost(const QString& what, std::function<void(NvHTTP&)> request);

    ComputerManager* m_ComputerManager;
    StreamProcessManager* m_Streams;
    QLocalServer m_HandOffServer;
    QTimer m_Timer;
    QString m_HostUuid;
    NvComputer* m_Computer = nullptr;
    bool m_Polling = false;
    bool m_Reached = false;
    int m_FailedPolls = 0;
    QDeadlineTimer m_ReachDeadline;
    QString m_PendingApp;

    // A host window's stream, with the window as it was last seen
    struct WindowStream
    {
        QString streamId;
        NvHostWindow window;
    };
    // The client window follows its host window: moves, minimize, restore; a new size needs
    // a stream of its own
    void followWindow(WindowStream& stream, const NvHostWindow& window);

    QHash<quint64, WindowStream> m_StreamOfWindow;
    QSet<quint64> m_ListedWindows;
    QSet<quint64> m_DismissedWindows;
};
