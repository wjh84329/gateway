#include "gatewayhttpserver.h"

#include <QAbstractSocket>
namespace {
constexpr int kMaxRequestBytes = 65536;

void SendHttpResponse(QTcpSocket *socket, int statusCode, const QByteArray &reason, const QByteArray &body, bool includeBody)
{
    QByteArray response = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(statusCode) + ' ' + reason + "\r\n";
    response += "Content-Type: application/json; charset=utf-8\r\n";
    response += "Connection: close\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
    if (includeBody) {
        response += body;
    }
    socket->write(response);
}
} // namespace

GatewayHttpServer &GatewayHttpServer::instance()
{
    // 不得 parent 到 qApp：静态单例若成为 QApplication 子对象，退出时会先被 ~QApplication 析构再被静态析构，双重释放（堆损坏 0xc0000374）。
    static GatewayHttpServer s_instance;
    return s_instance;
}

GatewayHttpServer::GatewayHttpServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &GatewayHttpServer::onNewConnection);
}

bool GatewayHttpServer::start(quint16 port, QString *errorMessage)
{
    stop();

    if (!m_server.listen(QHostAddress::Any, port)) {
        if (errorMessage) {
            *errorMessage = m_server.errorString();
        }
        return false;
    }

    return true;
}

void GatewayHttpServer::stop()
{
    m_server.close();
    m_requestBuffers.clear();
}

bool GatewayHttpServer::isListening() const
{
    return m_server.isListening();
}

quint16 GatewayHttpServer::serverPort() const
{
    return m_server.serverPort();
}

bool GatewayHttpServer::listenErrorIsAddressInUse() const
{
    if (m_server.serverError() == QAbstractSocket::AddressInUseError) {
        return true;
    }
    const QString s = m_server.errorString();
    return s.contains(QStringLiteral("already in use"), Qt::CaseInsensitive)
        || s.contains(QStringLiteral("Address in use"), Qt::CaseInsensitive)
        || s.contains(QStringLiteral("占用"))
        || s.contains(QStringLiteral("在使用"));
}

void GatewayHttpServer::onNewConnection()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        if (!socket) {
            continue;
        }

        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onSocketReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { forgetSocket(socket); });
    }
}

void GatewayHttpServer::forgetSocket(QTcpSocket *socket)
{
    m_requestBuffers.remove(socket);
}

void GatewayHttpServer::onSocketReadyRead(QTcpSocket *socket)
{
    if (!socket) {
        return;
    }

    QByteArray &buf = m_requestBuffers[socket];
    buf.append(socket->readAll());
    if (buf.size() > kMaxRequestBytes) {
        const QByteArray errBody = QByteArrayLiteral(R"({"status":"error","message":"request_too_large"})");
        SendHttpResponse(socket, 413, QByteArrayLiteral("Payload Too Large"), errBody, true);
        buf.clear();
        m_requestBuffers.remove(socket);
        socket->disconnectFromHost();
        return;
    }

    if (!buf.contains("\r\n")) {
        return;
    }

    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0 && buf.size() < 16384) {
        return;
    }

    const int firstLineEnd = buf.indexOf("\r\n");
    if (firstLineEnd <= 0) {
        const QByteArray errBody = QByteArrayLiteral(R"({"status":"error","message":"bad_request"})");
        SendHttpResponse(socket, 400, QByteArrayLiteral("Bad Request"), errBody, true);
        buf.clear();
        m_requestBuffers.remove(socket);
        socket->disconnectFromHost();
        return;
    }

    const QByteArray firstLine = buf.left(firstLineEnd);
    const QList<QByteArray> parts = firstLine.split(' ');
    const QByteArray method = parts.value(0).toUpper();
    QByteArray path = parts.value(1);
    const int qmark = path.indexOf('?');
    if (qmark >= 0) {
        path = path.left(qmark);
    }

    const bool isGet = (method == "GET");
    const bool isHead = (method == "HEAD");
    const QByteArray okBody = QByteArrayLiteral(R"({"status":"ok","service":"7x-gateway"})");

    if (isGet || isHead) {
        const bool health = (path == "/" || path == "/health");
        const bool includeEntity = !isHead;
        if (!health) {
            const QByteArray nf = QByteArrayLiteral(R"({"status":"error","message":"not_found"})");
            SendHttpResponse(socket, 404, QByteArrayLiteral("Not Found"), nf, includeEntity);
        } else {
            SendHttpResponse(socket, 200, QByteArrayLiteral("OK"), okBody, includeEntity);
        }
    } else {
        const QByteArray na = QByteArrayLiteral(R"({"status":"error","message":"method_not_allowed"})");
        SendHttpResponse(socket, 405, QByteArrayLiteral("Method Not Allowed"), na, true);
    }

    buf.clear();
    m_requestBuffers.remove(socket);
    socket->disconnectFromHost();
}
