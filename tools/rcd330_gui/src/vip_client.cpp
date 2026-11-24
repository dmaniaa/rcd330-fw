#include "vip_client.h"

#include <memory>

namespace {
constexpr int kMaxLogLines = 250;
}

VipClient::VipClient(QObject *parent) : QObject(parent) {
    m_reconnectTimer.setInterval(400);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &VipClient::attemptConnect);

    m_keepaliveTimer.setInterval(1500);
    connect(&m_keepaliveTimer, &QTimer::timeout, this, &VipClient::onKeepaliveTick);

    m_cmdTimeoutTimer.setInterval(1200);
    m_cmdTimeoutTimer.setSingleShot(true);
    connect(&m_cmdTimeoutTimer, &QTimer::timeout, this, &VipClient::onCmdTimeout);

    connect(&m_socket, &QTcpSocket::connected, this, &VipClient::onConnected);
    connect(&m_socket, &QTcpSocket::disconnected, this, &VipClient::onDisconnected);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &VipClient::onError);
    connect(&m_socket, &QTcpSocket::readyRead, this, &VipClient::onReadyRead);
}

VipClient::~VipClient() {
    disconnectFromServer();
}

void VipClient::connectToServer(const QString &host, quint16 port) {
    m_host = host;
    m_port = port;
    m_wantConnect = true;
    attemptConnect();
}

void VipClient::disconnectFromServer() {
    m_wantConnect = false;
    m_reconnectTimer.stop();
    m_keepaliveTimer.stop();
    m_cmdTimeoutTimer.stop();
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.abort();
    }
    failAllPending("DISCONNECTED");
    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false);
    }
}

void VipClient::attemptConnect() {
    if (!m_wantConnect) return;
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.abort();
    }
    m_rxBuf.clear();
    m_socket.connectToHost(m_host, m_port);
}

void VipClient::onConnected() {
    m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_rxBuf.clear();
    m_keepaliveTimer.start();
    // Verify handshake with initial PING if nothing is already queued
    if (m_queue.isEmpty() && !m_inFlight) {
        sendCommand("PING");
    } else {
        pumpNext();
    }
}

void VipClient::onDisconnected() {
    m_keepaliveTimer.stop();
    m_cmdTimeoutTimer.stop();
    failAllPending("DISCONNECTED");
    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false);
    }
    if (m_wantConnect) {
        m_reconnectTimer.start();
    }
}

void VipClient::onError(QAbstractSocket::SocketError) {
    m_keepaliveTimer.stop();
    m_cmdTimeoutTimer.stop();
    failAllPending("ERR socket");
    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false);
    }
    if (m_wantConnect && !m_reconnectTimer.isActive()) {
        m_reconnectTimer.start();
    }
}

bool VipClient::sendCommand(const QString &cmd, std::function<void(const QString &)> callback) {
    if (cmd.contains('\n') || cmd.contains('\r')) {
        appendLog("ERR invalid command (newline rejected)");
        if (callback) callback("ERR invalid command");
        return false;
    }
    const QString trimmed = cmd.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    m_queue.enqueue(PendingCmd{trimmed, std::move(callback)});
    pumpNext();
    return true;
}

void VipClient::sendCommands(const QStringList &cmds, std::function<void(const QStringList &)> callback) {
    if (cmds.isEmpty()) {
        if (callback) callback({});
        return;
    }
    auto replies = std::make_shared<QStringList>();
    auto remaining = std::make_shared<int>(cmds.size());
    for (const QString &c : cmds) {
        sendCommand(c, [replies, remaining, callback](const QString &r) {
            replies->append(r);
            if (--(*remaining) == 0 && callback) {
                callback(*replies);
            }
        });
    }
}

void VipClient::pumpNext() {
    if (m_inFlight || m_queue.isEmpty()) return;
    if (m_socket.state() != QAbstractSocket::ConnectedState) return;

    m_current = m_queue.dequeue();
    m_inFlight = true;
    const QByteArray wire = m_current.cmd.toUtf8() + '\n';
    m_socket.write(wire);
    m_socket.flush();
    m_cmdTimeoutTimer.start();
}

void VipClient::onReadyRead() {
    m_rxBuf.append(m_socket.readAll());
    while (true) {
        const int nl = m_rxBuf.indexOf('\n');
        if (nl < 0) break;
        const QString line = QString::fromUtf8(m_rxBuf.left(nl)).trimmed();
        m_rxBuf.remove(0, nl + 1);
        if (line.isEmpty()) continue;

        if (line.startsWith("HOST_TX ")) {
            appendLog(line);
            continue;
        }
        if (line.startsWith("REBOOT ")) {
            appendLog(line);
            emit rebootObserved();
            continue;
        }

        if (m_inFlight) {
            m_cmdTimeoutTimer.stop();
            const PendingCmd done = std::move(m_current);
            m_inFlight = false;
            if (!m_connected && (line.startsWith("PONG") || line.startsWith("OK"))) {
                m_connected = true;
                emit connectionChanged(true);
            }
            if (done.cmd != "PING") {
                appendLog(QString("> %1  |  %2").arg(done.cmd, line));
            }
            if (done.cb) {
                done.cb(line);
            }
            pumpNext();
        } else {
            appendLog(line);
        }
    }
}

void VipClient::onKeepaliveTick() {
    if (m_socket.state() != QAbstractSocket::ConnectedState) return;
    if (!m_inFlight && m_queue.isEmpty()) {
        sendCommand("PING");
    }
}

void VipClient::onCmdTimeout() {
    if (!m_inFlight) return;
    const PendingCmd timedOut = std::move(m_current);
    m_inFlight = false;
    if (timedOut.cmd != "PING") {
        appendLog(QString("> %1  |  TIMEOUT").arg(timedOut.cmd));
    }
    if (timedOut.cb) {
        timedOut.cb("TIMEOUT");
    }
    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false);
    }
    m_socket.abort();
    if (m_wantConnect) m_reconnectTimer.start();
}

void VipClient::failAllPending(const QString &reason) {
    if (m_inFlight) {
        const PendingCmd cur = std::move(m_current);
        m_inFlight = false;
        if (cur.cb) cur.cb(reason);
    }
    while (!m_queue.isEmpty()) {
        const PendingCmd p = m_queue.dequeue();
        if (p.cb) p.cb(reason);
    }
}

void VipClient::appendLog(const QString &line) {
    m_log.append(line);
    while (m_log.size() > kMaxLogLines) {
        m_log.removeFirst();
    }
    emit logAppended(line);
}

void VipClient::clearLog() {
    m_log.clear();
}
