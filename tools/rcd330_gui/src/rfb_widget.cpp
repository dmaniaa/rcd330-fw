#include "rfb_widget.h"

#include <QFocusEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

RfbScreenWidget::RfbScreenWidget(QWidget *parent) : QWidget(parent) {
    setMinimumSize(400, 240);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    resize(800, 480);
    setMouseTracking(false);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    m_reconnectTimer.setInterval(250);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &RfbScreenWidget::attemptConnect);

    connect(&m_socket, &QTcpSocket::connected, this, &RfbScreenWidget::onSocketConnected);
    connect(&m_socket, &QTcpSocket::disconnected, this, &RfbScreenWidget::onSocketDisconnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &RfbScreenWidget::onSocketReadyRead);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &RfbScreenWidget::onSocketError);
}

RfbScreenWidget::~RfbScreenWidget() {
    disconnectFromServer();
}

void RfbScreenWidget::connectToServer(const QString &host, quint16 port) {
    m_host = host;
    m_port = port;
    m_wantConnect = true;
    m_statusText = QString("Connecting to QEMU display (%1:%2)...").arg(host).arg(port);
    update();
    attemptConnect();
}

void RfbScreenWidget::disconnectFromServer() {
    m_wantConnect = false;
    m_reconnectTimer.stop();
    releaseTouchIfActive();
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.abort();
    }
    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false);
    }
}

void RfbScreenWidget::attemptConnect() {
    if (!m_wantConnect) return;
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.abort();
    }
    m_session.reset();
    m_socket.connectToHost(m_host, m_port);
}

void RfbScreenWidget::onSocketConnected() {
    m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_session.reset();
    m_statusText = "Negotiating display...";
    update();
}

void RfbScreenWidget::onSocketDisconnected() {
    m_touchDown = false;
    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false);
    }
    if (m_wantConnect) {
        m_statusText = "Reconnecting to QEMU display...";
        update();
        m_reconnectTimer.start();
    }
}

void RfbScreenWidget::onSocketError(QAbstractSocket::SocketError) {
    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false);
    }
    if (m_wantConnect && !m_reconnectTimer.isActive()) {
        m_statusText = "Waiting for QEMU display...";
        update();
        m_reconnectTimer.start();
    }
}

void RfbScreenWidget::onSocketReadyRead() {
    const QByteArray chunk = m_socket.readAll();
    QByteArray reply;
    bool dirty = false;
    QString err;
    const bool wasReady = (m_session.state() == RfbSession::State::Ready);

    if (!m_session.feed(chunk, reply, dirty, err)) {
        m_statusText = QString("Display protocol error: %1").arg(err);
        m_socket.abort();
        update();
        if (m_wantConnect) m_reconnectTimer.start();
        return;
    }

    if (!reply.isEmpty()) {
        m_socket.write(reply);
    }

    if (!wasReady && m_session.state() == RfbSession::State::Ready) {
        m_connected = true;
        emit connectionChanged(true);
        update();
    } else if (dirty) {
        update();
    }
}

QPoint RfbScreenWidget::mapWidgetToFramebuffer(const QPointF &pos) const {
    const int fbW = std::max(1, m_session.framebuffer().width());
    const int fbH = std::max(1, m_session.framebuffer().height());
    const double w = std::max(1.0, static_cast<double>(width()));
    const double h = std::max(1.0, static_cast<double>(height()));
    const int fx = std::clamp(static_cast<int>(std::lround(pos.x() * fbW / w)), 0, fbW - 1);
    const int fy = std::clamp(static_cast<int>(std::lround(pos.y() * fbH / h)), 0, fbH - 1);
    return QPoint(fx, fy);
}

void RfbScreenWidget::sendPointer(uint8_t mask, const QPoint &fbPos) {
    if (!m_connected || m_socket.state() != QAbstractSocket::ConnectedState) {
        return;
    }
    m_socket.write(RfbSession::buildPointerEvent(
        mask, static_cast<uint16_t>(fbPos.x()), static_cast<uint16_t>(fbPos.y())));
}

void RfbScreenWidget::releaseTouchIfActive() {
    if (m_touchDown) {
        m_touchDown = false;
        const QPoint p = (m_lastFbPos.x() >= 0) ? m_lastFbPos : QPoint(0, 0);
        sendPointer(0, p);
    }
}

void RfbScreenWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        m_touchDown = true;
        m_lastFbPos = mapWidgetToFramebuffer(event->position());
        sendPointer(1, m_lastFbPos);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void RfbScreenWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_touchDown) {
        const QPoint p = mapWidgetToFramebuffer(event->position());
        if (p != m_lastFbPos) {
            m_lastFbPos = p;
            sendPointer(1, m_lastFbPos);
        }
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void RfbScreenWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_touchDown) {
        m_touchDown = false;
        m_lastFbPos = mapWidgetToFramebuffer(event->position());
        sendPointer(0, m_lastFbPos);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void RfbScreenWidget::focusOutEvent(QFocusEvent *event) {
    releaseTouchIfActive();
    QWidget::focusOutEvent(event);
}

void RfbScreenWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    if (m_connected) {
        p.drawImage(rect(), m_session.framebuffer());
    } else {
        p.fillRect(rect(), QColor(0x0b, 0x0e, 0x13));
        p.setPen(QColor(0x8b, 0x97, 0xa8));
        QFont f = font();
        f.setPointSize(13);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, m_statusText);
    }
}
