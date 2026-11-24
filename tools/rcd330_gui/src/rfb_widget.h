#pragma once

#include "rfb_protocol.h"
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QTcpSocket>
#include <QTimer>
#include <QWidget>

class RfbScreenWidget : public QWidget {
    Q_OBJECT

public:
    explicit RfbScreenWidget(QWidget *parent = nullptr);
    ~RfbScreenWidget() override;

    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    bool isConnected() const { return m_connected; }
    QPoint mapWidgetToFramebuffer(const QPointF &pos) const;

signals:
    void connectionChanged(bool connected);

protected:
    QSize sizeHint() const override { return QSize(800, 480); }
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private slots:
    void attemptConnect();
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);

private:
    void sendPointer(uint8_t mask, const QPoint &fbPos);
    void releaseTouchIfActive();

    QTcpSocket m_socket;
    QTimer m_reconnectTimer;
    RfbSession m_session;
    QString m_host = "127.0.0.1";
    quint16 m_port = 5900;
    bool m_wantConnect = false;
    bool m_connected = false;
    bool m_touchDown = false;
    QPoint m_lastFbPos{-1, -1};
    QString m_statusText = "Connecting to QEMU display...";
};
