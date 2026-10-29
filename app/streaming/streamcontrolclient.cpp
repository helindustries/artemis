#include "streamcontrolclient.h"
#include "streaming/session.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

#include "SDL_compat.h"

#include <Limelight.h>

StreamControlClient::StreamControlClient(QString serverName, QString streamId)
    : m_Socket(nullptr),
      m_ServerName(serverName),
      m_StreamId(streamId)
{
    m_Thread.setObjectName("StreamControl");
    moveToThread(&m_Thread);
    connect(&m_Thread, &QThread::started, this, &StreamControlClient::connectToServer);
    m_Thread.start();
}

StreamControlClient::~StreamControlClient()
{
    m_Thread.quit();
    m_Thread.wait();
}

void StreamControlClient::reportState(QString state, QString detail)
{
    QMetaObject::invokeMethod(this, "sendState", Qt::QueuedConnection,
                              Q_ARG(QString, state), Q_ARG(QString, detail));
}

void StreamControlClient::reportFinalState(QString state, QString detail)
{
    // Queued behind any earlier report on the control thread, and sendState() flushes,
    // so the window has every message before the process goes
    QMetaObject::invokeMethod(this, [this, state, detail]() {
        sendState(state, detail);
        QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void StreamControlClient::connectToServer()
{
    m_Socket = new QLocalSocket(this);
    connect(m_Socket, &QLocalSocket::readyRead, this, &StreamControlClient::readCommands);
    connect(m_Socket, &QLocalSocket::disconnected, this, [this]() {
        // The controlling window went away. The stream carries on, it just has
        // nobody left to report to.
        qInfo() << "Stream control socket" << m_ServerName << "disconnected";
    });

    m_Socket->connectToServer(m_ServerName);
    if (m_Socket->waitForConnected(5000)) {
        sendState("starting", QString());
    }
    else {
        qWarning() << "Expected the controlling Artemis window on socket" << m_ServerName
                   << "but could not connect:" << m_Socket->errorString();
    }
}

void StreamControlClient::sendState(QString state, QString detail)
{
    if (m_Socket != nullptr && m_Socket->state() == QLocalSocket::ConnectedState) {
        QJsonObject message;
        message["id"] = m_StreamId;
        message["state"] = state;
        message["detail"] = detail;
        m_Socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
        m_Socket->flush();
    }
}

void StreamControlClient::readCommands()
{
    m_Buffer += m_Socket->readAll();

    int newline;
    while ((newline = m_Buffer.indexOf('\n')) >= 0) {
        QJsonObject message = QJsonDocument::fromJson(m_Buffer.left(newline)).object();
        m_Buffer.remove(0, newline + 1);
        handleCommand(message);
    }
}

void StreamControlClient::handleCommand(const QJsonObject& message)
{
    const QString command = message["command"].toString();
    if (command == "stop") {
        // SDL_QUIT ends a running session exactly as the disconnect key combo does.
        // Before or after a session there is no SDL loop to see it, so the Qt quit
        // covers those phases; while streaming it waits until the session returns.
        SDL_Event event;
        event.type = SDL_QUIT;
        event.quit.timestamp = SDL_GetTicks();
        SDL_PushEvent(&event);
        QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);
    }
    else if (command == "raise") {
        SDL_Event event;
        event.type = SDL_USEREVENT;
        event.user.code = SDL_CODE_RAISE_WINDOW;
        SDL_PushEvent(&event);
    }
    else if (command == "server-command") {
        // The index into the <ServerCommand> list the host sent; the host checks the permission
        const int index = message["index"].toInt(-1);
        if (index >= 0 && index <= UINT8_MAX) {
            // Unlike the other LiSend* calls this returns true on success, not 0
            const bool sent = LiSendExecServerCmd(static_cast<uint8_t>(index)) != 0;
            sendState("server-command", (sent ? QString("sent %1") : QString("failed %1")).arg(index));
        }
        else {
            qWarning() << "Stream control server-command needs an index from 0 to 255, got" << message["index"];
        }
    }
    else {
        qWarning() << "Unknown stream control command:" << command << "(expected stop, raise or server-command)";
    }
}
