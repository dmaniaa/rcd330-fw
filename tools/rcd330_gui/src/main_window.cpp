#include "main_window.h"
#include "rotary_knob_widget.h"

#include <QCloseEvent>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <cerrno>
#include <csignal>
#ifdef __APPLE__
#include <libproc.h>
#include <sys/proc.h>
#endif

static bool isProcessAlive(qint64 pid) {
    if (pid <= 0) return false;
    const pid_t p = static_cast<pid_t>(pid);
    if (::kill(p, 0) != 0 && errno == ESRCH) {
        return false;
    }
#ifdef __APPLE__
    struct proc_bsdinfo info {};
    const int ret = proc_pidinfo(p, PROC_PIDTBSDINFO, 0, &info, sizeof(info));
    if (ret <= 0) {
        return false;
    }
    if (info.pbi_status == SZOMB) {
        return false;
    }
#elif defined(__linux__)
    QFile f(QString("/proc/%1/stat").arg(pid));
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray stat = f.readAll();
    const qsizetype rparen = stat.lastIndexOf(')');
    if (rparen >= 0 && rparen + 2 < stat.size()) {
        const char state = stat[rparen + 2];
        if (state == 'Z' || state == 'X') {
            return false;
        }
    }
#endif
    return true;
}

MainWindow::MainWindow(const GuiConfig &cfg, QWidget *parent)
    : QMainWindow(parent), m_cfg(cfg) {
    setWindowTitle(QString("VW RCD 330 (%1)").arg(m_cfg.fwName));

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(10);

    // Top unified RCD 330 faceplate chassis
    auto *chassis = new QFrame(central);
    chassis->setObjectName("faceplate");
    auto *faceLayout = new QHBoxLayout(chassis);
    faceLayout->setContentsMargins(12, 12, 12, 12);
    faceLayout->setSpacing(12);

    // Left bezel column
    auto *leftCol = new QVBoxLayout();
    leftCol->setSpacing(10);
    leftCol->addWidget(makeBezelButton("RADIO", "RADIO", BezelButton::HoldMode::StandardHardkey, chassis));
    leftCol->addWidget(makeBezelButton("MEDIA", "MEDIA", BezelButton::HoldMode::StandardHardkey, chassis));
    leftCol->addWidget(makeBezelButton("PHONE", "PHONE", BezelButton::HoldMode::StandardHardkey, chassis));
    leftCol->addStretch(1);
    auto *volKnob = new RotaryKnobWidget("POWER / VOL", "−", "PWR", "POWER",
                                         BezelButton::HoldMode::PowerKey, "+", chassis);
    m_bezelButtons.append(volKnob->centerButton());
    connect(volKnob->centerButton(), &BezelButton::keyPressed, this, [this](const QString &k) {
        m_vip.sendCommand(QString("BTN %1 1").arg(k));
    });
    connect(volKnob->centerButton(), &BezelButton::keyReleased, this, [this](const QString &k) {
        m_vip.sendCommand(QString("BTN %1 0").arg(k));
    });
    connect(volKnob, &RotaryKnobWidget::stepped, this, [this](int delta) {
        m_vip.sendCommand(QString("ENC VOL %1").arg(delta));
    });
    leftCol->addWidget(volKnob);

    // Center: 800x480 touchscreen + sub-bezel control bar
    auto *centerCol = new QVBoxLayout();
    centerCol->setSpacing(8);
    m_screen = new RfbScreenWidget(chassis);
    centerCol->addWidget(m_screen, 0, Qt::AlignCenter);

    auto *subBar = new QHBoxLayout();
    subBar->setSpacing(8);
    auto *muteBtn = makeBezelButton("MUTE", "MUTE", BezelButton::HoldMode::InstantKey, chassis);
    muteBtn->setFixedSize(82, 34);
    auto *ejectBtn = makeBezelButton("EJECT", "EJECT", BezelButton::HoldMode::StandardHardkey, chassis);
    ejectBtn->setFixedSize(82, 34);

    m_statusBadge = new QLabel("Connecting...", chassis);
    m_statusBadge->setObjectName("statusBadge");

    m_dockToggleBtn = new QPushButton("Hide Controls ▾", chassis);
    m_dockToggleBtn->setFixedHeight(34);
    connect(m_dockToggleBtn, &QPushButton::clicked, this, &MainWindow::toggleDock);

    subBar->addWidget(muteBtn);
    subBar->addWidget(ejectBtn);
    subBar->addStretch(1);
    subBar->addWidget(m_statusBadge);
    subBar->addWidget(m_dockToggleBtn);
    centerCol->addLayout(subBar);

    // Right bezel column
    auto *rightCol = new QVBoxLayout();
    rightCol->setSpacing(10);
    rightCol->addWidget(makeBezelButton("VOICE", "VOICE", BezelButton::HoldMode::StandardHardkey, chassis));
    rightCol->addWidget(makeBezelButton("SETUP", "SETUP", BezelButton::HoldMode::StandardHardkey, chassis));
    rightCol->addWidget(makeBezelButton("APP", "APP", BezelButton::HoldMode::StandardHardkey, chassis));
    rightCol->addStretch(1);
    auto *tuneKnob = new RotaryKnobWidget("TUNE / OK", "◀", "OK", "TUNE_OK",
                                          BezelButton::HoldMode::StandardHardkey, "▶", chassis);
    m_bezelButtons.append(tuneKnob->centerButton());
    connect(tuneKnob->centerButton(), &BezelButton::keyPressed, this, [this](const QString &k) {
        m_vip.sendCommand(QString("BTN %1 1").arg(k));
    });
    connect(tuneKnob->centerButton(), &BezelButton::keyReleased, this, [this](const QString &k) {
        m_vip.sendCommand(QString("BTN %1 0").arg(k));
    });
    connect(tuneKnob, &RotaryKnobWidget::stepped, this, [this](int delta) {
        m_vip.sendCommand(QString("ENC TUNE %1").arg(delta));
    });
    rightCol->addWidget(tuneKnob);

    auto *leftWrap = new QWidget(chassis);
    leftWrap->setFixedWidth(134);
    leftWrap->setLayout(leftCol);
    auto *rightWrap = new QWidget(chassis);
    rightWrap->setFixedWidth(134);
    rightWrap->setLayout(rightCol);

    faceLayout->addWidget(leftWrap);
    faceLayout->addLayout(centerCol);
    faceLayout->addWidget(rightWrap);
    root->addWidget(chassis);

    // Bottom collapsible tabbed dock
    m_dock = new QTabWidget(central);
    m_vehicleTab = new VehicleTab(m_dock);
    CodingBridge bridge(m_cfg.codingScript, m_cfg.codingFile, m_cfg.fwName);
    m_codingTab = new CodingTab(bridge, &m_vip, m_dock);
    m_logTab = new LogTab(&m_vip, m_dock);

    m_dock->addTab(m_vehicleTab, "Vehicle (CAN / BAP) & Radio");
    m_dock->addTab(m_codingTab, "Headunit Coding (DID 0x0600)");
    m_dock->addTab(m_logTab, "VIP / IPCL Log");
    m_dock->setMaximumHeight(395);
    root->addWidget(m_dock);

    connect(m_vehicleTab, &VehicleTab::commandRequested, this, [this](const QString &cmd) {
        m_vip.sendCommand(cmd);
    });
    connect(m_vehicleTab, &VehicleTab::buttonTapRequested, this, [this](const QString &keyName) {
        m_vip.sendCommand(QString("BTN %1 1").arg(keyName));
        QTimer::singleShot(100, this, [this, keyName]() {
            m_vip.sendCommand(QString("BTN %1 0").arg(keyName));
        });
    });

    connect(&m_vip, &VipClient::connectionChanged, this, [this](bool ok) {
        updateStatusBadges();
        if (ok && !m_initialCodingSynced) {
            m_initialCodingSynced = true;
            m_codingTab->syncLoadedCodingToVip();
        }
    });
    connect(&m_vip, &VipClient::rebootObserved, this, [this]() {
        if (m_qemuExited) return;
        m_statusBadge->setText("Rebooting headunit (POWER 10s)...");
        if (m_vehicleTab) {
            m_vehicleTab->resetForReboot();
        }
        QTimer::singleShot(4000, this, &MainWindow::updateStatusBadges);
    });
    connect(m_screen, &RfbScreenWidget::connectionChanged, this, &MainWindow::updateStatusBadges);

    setCentralWidget(central);
    adjustSize();
    updateStatusBadges();

    if (m_cfg.autoConnect) {
        m_vip.connectToServer(m_cfg.vipHost, m_cfg.vipPort);
        m_screen->connectToServer(m_cfg.vncHost, m_cfg.vncPort);
    }

    if (m_cfg.qemuPid > 0) {
        m_qemuWatchTimer.setInterval(1000);
        connect(&m_qemuWatchTimer, &QTimer::timeout, this, &MainWindow::checkQemuLiveness);
        m_qemuWatchTimer.start();
    }
}

MainWindow::~MainWindow() {
    for (BezelButton *b : m_bezelButtons) {
        if (b) b->cancelHold();
    }
}

BezelButton *MainWindow::makeBezelButton(const QString &label, const QString &keyName,
                                         BezelButton::HoldMode mode, QWidget *parent) {
    auto *btn = new BezelButton(label, keyName, mode, parent);
    m_bezelButtons.append(btn);
    connect(btn, &BezelButton::keyPressed, this, [this](const QString &k) {
        m_vip.sendCommand(QString("BTN %1 1").arg(k));
    });
    connect(btn, &BezelButton::keyReleased, this, [this](const QString &k) {
        m_vip.sendCommand(QString("BTN %1 0").arg(k));
    });
    return btn;
}

bool MainWindow::isDockVisible() const {
    return !m_dock->isHidden();
}

void MainWindow::toggleDock() {
    const bool show = m_dock->isHidden();
    m_dock->setVisible(show);
    m_dockToggleBtn->setText(show ? "Hide Controls ▾" : "Show Controls ▴");
    adjustSize();
}

void MainWindow::updateStatusBadges() {
    if (m_qemuExited) return;
    const QString disp = m_screen->isConnected() ? "Display OK" : "Display ...";
    const QString vip = m_vip.isConnected() ? "VIP Connected" : "VIP ...";
    m_statusBadge->setText(QString("%1  |  %2").arg(disp, vip));
}

void MainWindow::checkQemuLiveness() {
    if (m_cfg.qemuPid <= 0 || m_qemuExited) return;
    if (!isProcessAlive(m_cfg.qemuPid)) {
        m_qemuExited = true;
        m_qemuWatchTimer.stop();
        m_screen->disconnectFromServer();
        m_vip.disconnectFromServer();
        m_statusBadge->setText("QEMU process exited");
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    for (BezelButton *b : m_bezelButtons) {
        if (b) b->cancelHold();
    }
    m_screen->disconnectFromServer();
    m_vip.disconnectFromServer();
    QMainWindow::closeEvent(event);
}
