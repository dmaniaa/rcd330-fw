#pragma once

#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>
#include <functional>

class VipClient : public QObject {
    Q_OBJECT

public:
    explicit VipClient(QObject *parent = nullptr);
    ~VipClient() override;

    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    bool isConnected() const { return m_connected; }

    bool sendCommand(const QString &cmd, std::function<void(const QString &)> callback = {});
    void sendCommands(const QStringList &cmds, std::function<void(const QStringList &)> callback = {});

    QStringList logLines() const { return m_log; }
    void clearLog();

signals:
    void connectionChanged(bool connected);
    void logAppended(const QString &line);
    void rebootObserved();

private slots:
    void attemptConnect();
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError);
    void onReadyRead();
    void onKeepaliveTick();
    void onCmdTimeout();

private:
    struct PendingCmd {
        QString cmd;
        std::function<void(const QString &)> cb;
    };

    void pumpNext();
    void appendLog(const QString &line);
    void failAllPending(const QString &reason);

    QTcpSocket m_socket;
    QTimer m_reconnectTimer;
    QTimer m_keepaliveTimer;
    QTimer m_cmdTimeoutTimer;
    QString m_host = "127.0.0.1";
    quint16 m_port = 4444;
    bool m_wantConnect = false;
    bool m_connected = false;
    bool m_inFlight = false;
    PendingCmd m_current;
    QQueue<PendingCmd> m_queue;
    QByteArray m_rxBuf;
    QStringList m_log;
};
