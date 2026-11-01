#pragma once

#include "backend/nvhttp.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

class NvComputer;

// SDL_USEREVENT code of a Dock icon activated: data1 is the program's name, a QString* for the
// main thread to delete
#define SDL_CODE_DOCK_RAISE 111

// Coherence mode's Dock icons (macOS): one per host program with windows, with the program's
// icon and name, each a copy of Artemis's dock tile app (coherencedocktile.m). A tile connects to
// this stream process over a Unix socket, asks for its program's windows when activated, and
// quits when its connection closes: the program has no windows left, or the stream ends.
class CoherenceDock
{
public:
    CoherenceDock(NvComputer* computer, const QString& hostName);
    ~CoherenceDock();

    // Main thread: the host's windows as listed; programs gain and lose their icons
    void update(const QVector<NvHostWindow>& windows);

private:
    void serve();
    void startTile(const QString& program, quint64 windowId);

    NvComputer* m_Computer;
    QString m_HostName;
    QString m_SocketPath;
    int m_Server = -1;
    // Written to wake serve() for a connection to close or the end
    int m_Wake[2] = { -1, -1 };
    std::atomic<bool> m_Stopping { false };
    std::thread m_ServerThread;

    // Programs with windows, and their tile's connection: -1 until the tile says hello. Only
    // serve() closes connections, those of programs gone wait in m_Closing: a connection closed
    // under its poll could be reused for another tile before poll returns
    std::mutex m_Lock;
    QHash<QString, int> m_Tiles;
    QVector<int> m_Closing;
    std::vector<std::thread> m_Starters;
};
