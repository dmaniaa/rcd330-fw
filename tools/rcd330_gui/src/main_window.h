#pragma once

#include "bezel_button.h"
#include "coding_bridge.h"
#include "coding_tab.h"
#include "log_tab.h"
#include "rfb_widget.h"
#include "vehicle_tab.h"
#include "vip_client.h"
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QVector>

struct GuiConfig {
    QString vipHost = "127.0.0.1";
    quint16 vipPort = 4444;
    QString vncHost = "127.0.0.1";
    quint16 vncPort = 5900;
    QString fwName = "RCD 330";
    QString codingFile;
    QString codingScript;
    qint64 qemuPid = 0;
    bool autoConnect = true;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const GuiConfig &cfg, QWidget *parent = nullptr);
    ~MainWindow() override;

    bool isDockVisible() const;
    void toggleDock();
    CodingTab *codingTab() const { return m_codingTab; }

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void updateStatusBadges();
    void checkQemuLiveness();

private:
    BezelButton *makeBezelButton(const QString &label, const QString &keyName,
                                 BezelButton::HoldMode mode, QWidget *parent);

    GuiConfig m_cfg;
    VipClient m_vip;
    RfbScreenWidget *m_screen = nullptr;
    QTabWidget *m_dock = nullptr;
    QPushButton *m_dockToggleBtn = nullptr;
    QLabel *m_statusBadge = nullptr;
    VehicleTab *m_vehicleTab = nullptr;
    CodingTab *m_codingTab = nullptr;
    LogTab *m_logTab = nullptr;
    QVector<BezelButton *> m_bezelButtons;
    QTimer m_qemuWatchTimer;
    bool m_initialCodingSynced = false;
    bool m_qemuExited = false;
};
