#pragma once

#include "identitymanager.h"
#include "nvapp.h"
#include "nvaddress.h"

#include <Limelight.h>

#include <QRect>
#include <QSize>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class NvComputer;

class NvDisplayMode
{
public:
    bool operator==(const NvDisplayMode& other) const
    {
        return width == other.width &&
                height == other.height &&
                refreshRate == other.refreshRate;
    }

    int width;
    int height;
    int refreshRate;
};
Q_DECLARE_TYPEINFO(NvDisplayMode, Q_PRIMITIVE_TYPE);

// A host window coherence mode streams on its own (our host's /windows)
struct NvHostWindow
{
    quint64 id = 0;
    QString title;
    QString process;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool minimized = false;
    // A menu, tooltip or other untitled window, shown over the others
    bool popup = false;
    // Its picture in the host's coherence atlas; empty when the atlas does not show it
    QRect atlas;
};

// An app the host's user pinned, which coherence mode offers as a launcher here
struct NvPinnedApp
{
    QString id;
    QString name;
};

class GfeHttpResponseException : public std::exception
{
public:
    GfeHttpResponseException(int statusCode, QString message) :
        m_StatusCode(statusCode),
        m_StatusMessage(message)
    {

    }

    const char* what() const throw()
    {
        return m_StatusMessage.toLatin1();
    }

    const char* getStatusMessage() const
    {
        return m_StatusMessage.toLatin1();
    }

    int getStatusCode() const
    {
        return m_StatusCode;
    }

    QString toQString() const
    {
        return m_StatusMessage + " (Error " + QString::number(m_StatusCode) + ")";
    }

private:
    int m_StatusCode;
    QString m_StatusMessage;
};

class QtNetworkReplyException : public std::exception
{
public:
    QtNetworkReplyException(QNetworkReply::NetworkError error, QString errorText) :
        m_Error(error),
        m_ErrorText(errorText)
    {

    }

    const char* what() const throw()
    {
        return m_ErrorText.toLatin1();
    }

    const char* getErrorText() const
    {
        return m_ErrorText.toLatin1();
    }

    QNetworkReply::NetworkError getError() const
    {
        return m_Error;
    }

    QString toQString() const
    {
        return m_ErrorText + " (Error " + QString::number(m_Error) + ")";
    }

private:
    QNetworkReply::NetworkError m_Error;
    QString m_ErrorText;
};

class NvHTTP : public QObject
{
    Q_OBJECT

public:
    enum NvLogLevel {
        NVLL_NONE,
        NVLL_ERROR,
        NVLL_VERBOSE
    };

    explicit NvHTTP(NvAddress address, uint16_t httpsPort, QSslCertificate serverCert);

    explicit NvHTTP(NvComputer* computer);

    static
    int
    getCurrentGame(QString serverInfo);

    QString
    getServerInfo(NvLogLevel logLevel, bool fastFail = false);

    static
    void
    verifyResponseStatus(QString xml);

    static
    QString
    getXmlString(QString xml,
                 QString tagName);

    static
    QStringList
    getXmlArray(QString xml,
                QString tagName);

    static
    bool
    hasXmlElement(QString xml,
                  QString tagName);

    static
    QByteArray
    getXmlStringFromHex(QString xml,
                        QString tagName);

    QString
    openConnectionToString(QUrl baseUrl,
                           QString command,
                           QString arguments,
                           int timeoutMs,
                           NvLogLevel logLevel = NvLogLevel::NVLL_VERBOSE);

    void setServerCert(QSslCertificate serverCert);

    void setAddress(NvAddress address);
    void setHttpsPort(uint16_t port);

    NvAddress address();

    QSslCertificate serverCert();

    uint16_t httpPort();

    uint16_t httpsPort();

    static
    QVector<int>
    parseQuad(QString quad);

    void
    quitApp();

    void
    startApp(QString verb,
             bool isGfe,
             int appId,
             QString appUuid,
             PSTREAM_CONFIGURATION streamConfig,
             bool sops,
             bool localAudio,
             int gamepadMask,
             bool persistGameControllersOnDisconnect,
             QString& rtspSessionUrl);

    QVector<NvApp>
    getAppList();

    // With screens (their free areas relative to the top-left of all of them, the primary first),
    // the host gathers its windows on screens of those sizes, arranged alike (coherence mode),
    // and onScreen tells whether it did: positions are then relative to the screens. atlasSize is
    // the size of the screens' atlas, invalid where the host draws none
    QVector<NvHostWindow>
    getWindows(const QVector<QRect>& screens = {}, bool* onScreen = nullptr, QSize* atlasSize = nullptr);

    QVector<NvPinnedApp>
    getPinnedApps();

    QImage
    getPinnedAppIcon(const QString& id);

    // Coherence mode: the icon of the program a host window belongs to
    QImage
    getWindowIcon(quint64 id);

    void
    launchPinnedApp(const QString& id);

    // Coherence mode: brings a host window to the front, restoring it when minimized
    void
    raiseWindow(quint64 id);

    // Coherence mode: opens the host's Start menu on our coherence screen
    void
    openCoherenceStartMenu();

    // Coherence mode: asks a host window to close, as its close button does
    void
    closeWindow(quint64 id);

    // With a position (relative to the host's coherence screen), the window moves there too,
    // as a resize by its left or top edge does
    void
    resizeWindow(quint64 id, int width, int height, const QPoint* position = nullptr);

    // Coherence mode ended: the host removes the screen it gathered our windows on
    void
    stopCoherence();

    // Where a host window is on its coherence screen, relative to the screen's work area
    QRect
    getWindowPosition(quint64 id);

    QImage
    getBoxArt(int appId);

    // Artemis clipboard sync endpoints (Apollo servers only)
    QString
    getClipboardContent();

    bool
    sendClipboardContent(const QString& content);

    // Server command methods are now handled through LiSendExecServerCmd in the moonlight-common-c library

    static
    QVector<NvDisplayMode>
    getDisplayModeList(QString serverInfo);

    QUrl m_BaseUrlHttp;
    QUrl m_BaseUrlHttps;
private:
    void
    handleSslErrors(QNetworkReply* reply, const QList<QSslError>& errors);

    QNetworkReply*
    openConnection(QUrl baseUrl,
                   QString command,
                   QString arguments,
                   int timeoutMs,
                   NvLogLevel logLevel);

    NvAddress m_Address;
    QNetworkAccessManager m_Nam;
    QSslCertificate m_ServerCert;
};
