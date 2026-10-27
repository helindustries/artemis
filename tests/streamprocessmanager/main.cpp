// Headless test of StreamProcessManager. The manager starts its own executable as the
// stream process, so this program is both: run without arguments it is the test, run
// with "stream ..." it is a fake stream that speaks the control protocol.
//
// The fake stream writes its arguments to $FAKE_STREAM_ARGS, reports
// starting/connecting/streaming, ends on "stop", and ignores everything when
// $FAKE_STREAM_DEAF is set, so the manager has to kill it.

#include "backend/streamprocessmanager.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <functional>

static int s_Failures = 0;

static void expect(bool condition, const QString& message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", qPrintable(message));
        s_Failures++;
    }
}

static bool waitFor(const std::function<bool()>& condition, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return condition();
}

static QStringList readArguments(const QString& path)
{
    QStringList arguments;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        arguments = QString::fromUtf8(file.readAll()).split('\n');
    }
    return arguments;
}

static int runFakeStream(const QStringList& arguments)
{
    QFile record(qEnvironmentVariable("FAKE_STREAM_ARGS"));
    if (record.open(QIODevice::WriteOnly)) {
        record.write(arguments.join('\n').toUtf8());
        record.close();
    }

    QString socketName = arguments.value(arguments.indexOf("--control-socket") + 1);
    QString streamId = arguments.value(arguments.indexOf("--stream-id") + 1);
    bool deaf = qEnvironmentVariableIsSet("FAKE_STREAM_DEAF");

    QLocalSocket socket;
    socket.connectToServer(socketName);
    if (!socket.waitForConnected(5000)) {
        fprintf(stderr, "fake stream: expected control socket %s\n", qPrintable(socketName));
        return 3;
    }

    auto send = [&](const QString& state) {
        QJsonObject message { { "id", streamId }, { "state", state }, { "detail", "" } };
        socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
        socket.flush();
    };
    send("starting");
    send("connecting");
    send("streaming");

    QObject::connect(&socket, &QLocalSocket::readyRead, [&]() {
        QByteArray data = socket.readAll();
        if (!deaf && data.contains("\"stop\"")) {
            send("ended");
            socket.waitForBytesWritten(1000);
            QCoreApplication::quit();
        }
    });
    return QCoreApplication::exec();
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QStringList arguments = app.arguments().mid(1);
    if (arguments.value(0) == "stream") {
        return runFakeStream(arguments);
    }

    QTemporaryDir directory;
    QString record = directory.filePath("args");
    qputenv("FAKE_STREAM_ARGS", record.toUtf8());

    StreamProcessManager manager;

    // A profile becomes the stream's options, and its state reaches the model
    QVariantMap profile { { "name", "MacBook" }, { "width", 1728 }, { "height", 1116 }, { "fps", 120 },
                          { "bitrateKbps", 40000 }, { "displayMode", "borderless" }, { "screen", "Built-in" } };
    manager.start("EB8319F3-HOST", "Heimdall", "Desktop", profile);
    expect(manager.count() == 1, "expected one stream after start");
    expect(waitFor([&]() { return manager.data(manager.index(0), StreamProcessManager::StateRole) == "streaming"; }, 10000),
           "expected the stream to report streaming");
    expect(manager.data(manager.index(0), StreamProcessManager::ProfileNameRole) == "MacBook",
           "expected the profile name in the model");
    expect(manager.data(manager.index(0), StreamProcessManager::HostNameRole) == "Heimdall",
           "expected the host's name in the model, not its id");

    // The host is found by its id, so renaming it cannot break a stored launch
    QStringList passed = readArguments(record);
    QStringList wanted { "stream", "EB8319F3-HOST", "Desktop", "--resolution", "1728x1116", "--fps", "120",
                         "--bitrate", "40000", "--display-mode", "borderless", "--screen", "Built-in" };
    for (const QString& argument : wanted) {
        expect(passed.contains(argument), QString("expected stream argument %1, got %2").arg(argument, passed.join(' ')));
    }
    expect(!passed.contains("Heimdall"), QString("expected the host id, not its name, got %1").arg(passed.join(' ')));

    // Stop ends it cleanly and it leaves the list
    QElapsedTimer stopTimer;
    stopTimer.start();
    manager.stop(0);
    expect(waitFor([&]() { return manager.count() == 0; }, 5000), "expected the stream to leave the list after stop");
    expect(stopTimer.elapsed() < 5000, QString("expected a clean stop well before the kill, took %1 ms").arg(stopTimer.elapsed()));

    // An empty profile passes no settings, so the stream uses the global ones; without an
    // id the host is found by name
    manager.start("", "Heimdall", "Desktop", QVariantMap());
    waitFor([&]() { return manager.data(manager.index(0), StreamProcessManager::StateRole) == "streaming"; }, 10000);
    passed = readArguments(record);
    expect(!passed.contains("--resolution") && !passed.contains("--bitrate"),
           QString("expected no settings for an empty profile, got %1").arg(passed.join(' ')));
    expect(passed.value(1) == "Heimdall", QString("expected the host name without an id, got %1").arg(passed.join(' ')));
    manager.stop(0);
    waitFor([&]() { return manager.count() == 0; }, 5000);

    // A stream that ignores stop is killed after the grace period and still leaves
    qputenv("FAKE_STREAM_DEAF", "1");
    manager.start("EB8319F3-HOST", "Heimdall", "Desktop", profile);
    waitFor([&]() { return manager.data(manager.index(0), StreamProcessManager::StateRole) == "streaming"; }, 10000);
    manager.stop(0);
    expect(waitFor([&]() { return manager.count() == 0; }, 15000), "expected a stream that ignores stop to be killed and leave the list");
    qunsetenv("FAKE_STREAM_DEAF");

    // A stream that dies on its own leaves the list
    manager.start("EB8319F3-HOST", "Heimdall", "Desktop", profile);
    waitFor([&]() { return manager.data(manager.index(0), StreamProcessManager::StateRole) == "streaming"; }, 10000);
    QProcess::execute("pkill", { "-KILL", "-f", QCoreApplication::applicationFilePath() + " stream" });
    expect(waitFor([&]() { return manager.count() == 0; }, 5000), "expected a killed stream to leave the list");

    printf("%s\n", s_Failures == 0 ? "all checks passed" : "checks failed");
    return s_Failures == 0 ? 0 : 1;
}
