#pragma once

#include <QObject>
#include <QThread>

class QJsonObject;
class QLocalSocket;

// The stream process's end of the control socket to the Artemis window that started it.
// It lives on its own thread: while a session streams, the main thread runs SDL's event
// loop and Qt's never gets a turn, so a socket on the main thread would go deaf.
class StreamControlClient : public QObject
{
    Q_OBJECT

public:
    StreamControlClient(QString serverName, QString streamId);
    ~StreamControlClient();

    // Safe to call from any thread
    void reportState(QString state, QString detail = QString());

private slots:
    void connectToServer();
    void readCommands();
    void sendState(QString state, QString detail);

private:
    void handleCommand(const QJsonObject& message);

    QThread m_Thread;
    QLocalSocket* m_Socket;
    QString m_ServerName;
    QString m_StreamId;
    QByteArray m_Buffer;
};
