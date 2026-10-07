#include "singleinstance.h"

#include <QLocalSocket>

SingleInstance::SingleInstance(const QString &key, QObject *parent)
    : QObject(parent)
    , m_key(key)
{
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *sock = m_server.nextPendingConnection()) {
            connect(sock, &QLocalSocket::disconnected, sock, &QLocalSocket::deleteLater);
            connect(sock, &QLocalSocket::readyRead, sock, [this, sock] {
                const QByteArray cmd = sock->readAll().trimmed();
                if (cmd == QByteArrayLiteral("activate"))
                    emit activated();
                sock->disconnectFromServer();
            });
        }
    });
}

SingleInstance::~SingleInstance()
{
    m_server.close();
}

bool SingleInstance::tryLock()
{
    // 先探测：能连上说明已有实例
    QLocalSocket probe;
    probe.connectToServer(m_key);
    if (probe.waitForConnected(300)) {
        probe.disconnectFromServer();
        return false;
    }
    QLocalServer::removeServer(m_key);   // 清理上次异常退出留下的 socket
    return m_server.listen(m_key);
}

void SingleInstance::notifyExisting()
{
    QLocalSocket sock;
    sock.connectToServer(m_key);
    if (sock.waitForConnected(500)) {
        sock.write(QByteArrayLiteral("activate"));
        sock.flush();
        sock.waitForBytesWritten(500);
        sock.disconnectFromServer();
    }
}
