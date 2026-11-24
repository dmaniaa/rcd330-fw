#include "vehicle_tab.h"

#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

VehicleTab::VehicleTab(QWidget *parent) : QWidget(parent) {
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(12);

    // Left column: Emulated Car Components (CAN / BAP)
    auto *canBox = new QGroupBox("Emulated Car Components (CAN / BAP)", this);
    auto *canGrid = new QGridLayout(canBox);
    canGrid->setVerticalSpacing(5);
    canGrid->setHorizontalSpacing(10);
    int row = 0;

    // Ignition Clamp
    canGrid->addWidget(new QLabel("Ignition Clamp", canBox), row, 0);
    auto *ignRow = new QHBoxLayout();
    const char *ignLabels[3] = {"OFF", "CL_S (ACC)", "CL_15 (IGN)"};
    for (int i = 0; i < 3; ++i) {
        m_ignBtns[i] = new QPushButton(ignLabels[i], canBox);
        m_ignBtns[i]->setCheckable(true);
        connect(m_ignBtns[i], &QPushButton::clicked, this, [this, i]() {
            setIgnitionVisual(i);
            emit commandRequested(QString("IGN %1").arg(i));
        });
        ignRow->addWidget(m_ignBtns[i]);
    }
    setIgnitionVisual(2);
    canGrid->addLayout(ignRow, row++, 1, 1, 2);

    // Gear / RVC
    canGrid->addWidget(new QLabel("Gear / RVC", canBox), row, 0);
    auto *revRow = new QHBoxLayout();
    auto *drvBtn = new QPushButton("Drive / Park", canBox);
    auto *revBtn = new QPushButton("Reverse (RVC)", canBox);
    revBtn->setObjectName("warnBtn");
    connect(drvBtn, &QPushButton::clicked, this, [this]() { emit commandRequested("REV 0"); });
    connect(revBtn, &QPushButton::clicked, this, [this]() { emit commandRequested("REV 1"); });
    revRow->addWidget(drvBtn);
    revRow->addWidget(revBtn);
    canGrid->addLayout(revRow, row++, 1, 1, 2);

    // Sliders helper
    auto addSlider = [&](const QString &label, const QString &objName,
                         int minV, int maxV, int initV, int step,
                         const QString &cmdPrefix, const QString &suffix,
                         QSlider **outSlider, QLabel **outLabel) {
        canGrid->addWidget(new QLabel(label, canBox), row, 0);
        auto *sl = new QSlider(Qt::Horizontal, canBox);
        sl->setObjectName(objName);
        sl->setRange(minV, maxV);
        sl->setSingleStep(step);
        sl->setPageStep(step);
        sl->setValue(initV);
        auto *valLbl = new QLabel(QString("%1%2").arg(initV).arg(suffix), canBox);
        valLbl->setMinimumWidth(74);
        valLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        connect(sl, &QSlider::valueChanged, this, [this, valLbl, cmdPrefix, suffix, step](int v) {
            const int snapped = (step > 1) ? ((v / step) * step) : v;
            const QString nextText = QString("%1%2").arg(snapped).arg(suffix);
            if (valLbl->text() != nextText) {
                valLbl->setText(nextText);
                emit commandRequested(QString("%1 %2").arg(cmdPrefix).arg(snapped));
            }
        });
        canGrid->addWidget(sl, row, 1);
        canGrid->addWidget(valLbl, row, 2);
        *outSlider = sl;
        *outLabel = valLbl;
        ++row;
    };

    addSlider("Vehicle Speed", "speedSlider", 0, 240, 0, 1, "SPEED", " km/h", &m_speedSlider, &m_speedVal);
    addSlider("Engine RPM", "rpmSlider", 0, 7000, 800, 100, "RPM", "", &m_rpmSlider, &m_rpmVal);
    addSlider("Exterior Temp", "tempSlider", -30, 50, 21, 1, "TEMP", " °C", &m_tempSlider, &m_tempVal);

    // CL_58 Illumination (PQ CL_58 0/1, MQB CL_58d/CL_58s 0..100%, CarPlay/AA night mode)
    auto *cl58Lbl = new QLabel("CL_58 (Lights)", canBox);
    cl58Lbl->setToolTip(
        "Terminal 58 (Klemme 58): parking & instrument panel illumination.\n"
        "PQ ETM shows CL_58 (0/1); MQB shows CL_58d/CL_58s (0–100%).\n"
        "Also toggles CarPlay / Android Auto / MirrorLink night mode.");
    canGrid->addWidget(cl58Lbl, row, 0);
    auto *cl58Ctrl = new QHBoxLayout();
    cl58Ctrl->setSpacing(6);
    m_cl58OffBtn = new QPushButton("OFF", canBox);
    m_cl58OffBtn->setObjectName("cl58OffBtn");
    m_cl58OffBtn->setCheckable(true);
    m_cl58OffBtn->setFixedWidth(48);
    m_cl58OnBtn = new QPushButton("ON", canBox);
    m_cl58OnBtn->setObjectName("cl58OnBtn");
    m_cl58OnBtn->setCheckable(true);
    m_cl58OnBtn->setFixedWidth(48);
    m_cl58Slider = new QSlider(Qt::Horizontal, canBox);
    m_cl58Slider->setObjectName("cl58Slider");
    m_cl58Slider->setRange(0, 100);
    m_cl58Slider->setSingleStep(5);
    m_cl58Slider->setPageStep(10);
    m_cl58Slider->setValue(0);
    m_cl58Val = new QLabel("OFF", canBox);
    m_cl58Val->setMinimumWidth(74);
    m_cl58Val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_cl58OffBtn, &QPushButton::clicked, this, [this]() {
        if (m_cl58Slider->value() != 0) {
            m_cl58Slider->setValue(0);
        } else {
            updateCl58Visual(0);
            emit commandRequested("CL58 0");
        }
    });
    connect(m_cl58OnBtn, &QPushButton::clicked, this, [this]() {
        if (m_cl58Slider->value() == 0) {
            m_cl58Slider->setValue(100);
        } else {
            updateCl58Visual(m_cl58Slider->value());
            emit commandRequested(QString("CL58 %1").arg(m_cl58Slider->value()));
        }
    });
    connect(m_cl58Slider, &QSlider::valueChanged, this, [this](int v) {
        updateCl58Visual(v);
        emit commandRequested(QString("CL58 %1").arg(v));
    });
    updateCl58Visual(0);
    cl58Ctrl->addWidget(m_cl58OffBtn);
    cl58Ctrl->addWidget(m_cl58OnBtn);
    cl58Ctrl->addWidget(m_cl58Slider, 1);
    canGrid->addLayout(cl58Ctrl, row, 1);
    canGrid->addWidget(m_cl58Val, row++, 2);

    // Ambient Light Sensor (PQ CAN 0..253)
    auto *lightLbl = new QLabel("Light Sensor", canBox);
    lightLbl->setToolTip(
        "PQ ambient light sensor (0 = pitch dark, 253 = bright daylight).\n"
        "Reported live in Engineering Testmode -> SW -> Car -> Light sensor.");
    canGrid->addWidget(lightLbl, row, 0);
    m_lightSlider = new QSlider(Qt::Horizontal, canBox);
    m_lightSlider->setObjectName("lightSlider");
    m_lightSlider->setRange(0, 253);
    m_lightSlider->setValue(128);
    m_lightVal = new QLabel(canBox);
    m_lightVal->setMinimumWidth(74);
    m_lightVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_lightSlider, &QSlider::valueChanged, this, [this](int v) {
        updateLightVisual(v);
        emit commandRequested(QString("LIGHT %1").arg(v));
    });
    updateLightVisual(128);
    canGrid->addWidget(m_lightSlider, row, 1);
    canGrid->addWidget(m_lightVal, row++, 2);

    // Doors Open
    canGrid->addWidget(new QLabel("Doors Open", canBox), row, 0);
    auto *doorRow = new QHBoxLayout();
    const struct { const char *lbl; const char *cmd; } doors[] = {
        {"All Closed", "DOORS 0"}, {"FL", "DOORS 1"}, {"FR", "DOORS 2"}, {"All Open", "DOORS 15"}
    };
    for (const auto &d : doors) {
        auto *b = new QPushButton(d.lbl, canBox);
        connect(b, &QPushButton::clicked, this, [this, c = QString(d.cmd)]() { emit commandRequested(c); });
        doorRow->addWidget(b);
    }
    canGrid->addLayout(doorRow, row++, 1, 1, 2);

    // OPS Parking
    canGrid->addWidget(new QLabel("OPS Parking", canBox), row, 0);
    auto *opsRow = new QHBoxLayout();
    const struct { const char *lbl; const char *cmd; } ops[] = {
        {"Clear / Close", "OPS 255 255 255 255"},
        {"Close", "OPS 40 30 30 40"},
        {"Critical", "OPS 10 10 10 10"},
    };
    for (const auto &o : ops) {
        auto *b = new QPushButton(o.lbl, canBox);
        connect(b, &QPushButton::clicked, this, [this, c = QString(o.cmd)]() { emit commandRequested(c); });
        opsRow->addWidget(b);
    }
    canGrid->addLayout(opsRow, row++, 1, 1, 2);

    // Cluster Clock
    canGrid->addWidget(new QLabel("Cluster Clock", canBox), row, 0);
    auto *timeBtn = new QPushButton("Sync Host Time to Headunit", canBox);
    connect(timeBtn, &QPushButton::clicked, this, [this]() {
        const QDateTime now = QDateTime::currentDateTime();
        emit commandRequested(QString("TIME %1 %2 %3 %4 %5 %6")
                                  .arg(now.date().year())
                                  .arg(now.date().month())
                                  .arg(now.date().day())
                                  .arg(now.time().hour())
                                  .arg(now.time().minute())
                                  .arg(now.time().second()));
    });
    canGrid->addWidget(timeBtn, row++, 1, 1, 2);

    root->addWidget(canBox, 3);

    // Right column: 12V Battery (BEM) + SWC + FM/RDS Radio Simulator
    auto *rightCol = new QVBoxLayout();
    rightCol->setSpacing(8);

    auto *battBox = new QGroupBox("12V Battery & Low-Voltage Warning (BEM)", this);
    auto *battLayout = new QVBoxLayout(battBox);
    battLayout->setSpacing(5);
    battLayout->setContentsMargins(10, 6, 10, 6);

    auto *battSlRow = new QHBoxLayout();
    m_battSlider = new QSlider(Qt::Horizontal, battBox);
    m_battSlider->setObjectName("battSlider");
    m_battSlider->setRange(60, 150);
    m_battSlider->setSingleStep(1);
    m_battSlider->setPageStep(5);
    m_battSlider->setValue(126);
    m_battVal = new QLabel(battBox);
    m_battVal->setMinimumWidth(78);
    m_battVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_battSlider, &QSlider::valueChanged, this, [this](int dv) {
        updateBatteryVisual(dv);
        emit commandRequested(QString("BATT %1").arg(dv));
    });
    updateBatteryVisual(126);
    battSlRow->addWidget(m_battSlider, 1);
    battSlRow->addWidget(m_battVal);
    battLayout->addLayout(battSlRow);

    auto *battBtnRow = new QHBoxLayout();
    auto *battOkBtn = new QPushButton("12.6V OK", battBox);
    battOkBtn->setObjectName("battOkBtn");
    auto *battLowBtn = new QPushButton("11.2V Low", battBox);
    battLowBtn->setObjectName("battLowBtn");
    auto *battCritBtn = new QPushButton("10.2V Crit", battBox);
    battCritBtn->setObjectName("battCritBtn");
    auto setBattPreset = [this](int dv) {
        if (m_battSlider->value() != dv) {
            m_battSlider->setValue(dv);
        } else {
            updateBatteryVisual(dv);
            emit commandRequested(QString("BATT %1").arg(dv));
        }
    };
    connect(battOkBtn, &QPushButton::clicked, this, [setBattPreset]() { setBattPreset(126); });
    connect(battLowBtn, &QPushButton::clicked, this, [setBattPreset]() { setBattPreset(112); });
    connect(battCritBtn, &QPushButton::clicked, this, [setBattPreset]() { setBattPreset(102); });
    battBtnRow->addWidget(battOkBtn);
    battBtnRow->addWidget(battLowBtn);
    battBtnRow->addWidget(battCritBtn);
    battLayout->addLayout(battBtnRow);

    auto *battHint = new QLabel("PQ: low/crit popups appear with CL_15 OFF (CL_S/ACC)", battBox);
    battHint->setStyleSheet("color: #8b949e; font-size: 11px;");
    battLayout->addWidget(battHint);
    rightCol->addWidget(battBox);

    auto *swcBox = new QGroupBox("Steering Wheel Controls (MFSW CAN)", this);
    auto *swcGrid = new QGridLayout(swcBox);
    const struct { const char *lbl; const char *cmd; bool isKeyTap; } swc[] = {
        {"VOL-", "SWC VOL-", false},
        {"VOL+", "SWC VOL+", false},
        {"◀ PREV", "ENC TUNE -1", false},
        {"NEXT ▶", "ENC TUNE 1", false},
        {"VOICE", "VOICE", true},
        {"MUTE", "SWC MUTE", false},
    };
    for (int i = 0; i < 6; ++i) {
        auto *b = new QPushButton(swc[i].lbl, swcBox);
        if (swc[i].isKeyTap) {
            connect(b, &QPushButton::clicked, this, [this, k = QString(swc[i].cmd)]() { emit buttonTapRequested(k); });
        } else {
            connect(b, &QPushButton::clicked, this, [this, c = QString(swc[i].cmd)]() { emit commandRequested(c); });
        }
        swcGrid->addWidget(b, i / 3, i % 3);
    }
    rightCol->addWidget(swcBox);

    auto *tunerBox = new QGroupBox("FM / RDS Radio Station Simulator", this);
    auto *tunerGrid = new QGridLayout(tunerBox);
    const struct { const char *lbl; const char *cmd; } stations[] = {
        {"87.5 VW RADIO", "TUNER 8750 VW RADIO"},
        {"91.2 BAYERN 3", "TUNER 9120 BAYERN 3"},
        {"95.8 RCD 330", "TUNER 9580 RCD 330"},
        {"98.5 SWR3", "TUNER 9850 SWR3"},
        {"101.4 RADIO 1", "TUNER 10140 RADIO 1"},
        {"104.6 CLASSIC", "TUNER 10460 CLASSIC"},
    };
    for (int i = 0; i < 6; ++i) {
        auto *b = new QPushButton(stations[i].lbl, tunerBox);
        connect(b, &QPushButton::clicked, this, [this, c = QString(stations[i].cmd)]() { emit commandRequested(c); });
        tunerGrid->addWidget(b, i / 2, i % 2);
    }
    rightCol->addWidget(tunerBox);
    rightCol->addStretch(1);

    root->addLayout(rightCol, 2);
}

void VehicleTab::resetForReboot() {
    setIgnitionVisual(2);
    if (m_speedSlider) {
        QSignalBlocker b(m_speedSlider);
        m_speedSlider->setValue(0);
        if (m_speedVal) m_speedVal->setText("0 km/h");
    }
    if (m_rpmSlider) {
        QSignalBlocker b(m_rpmSlider);
        m_rpmSlider->setValue(800);
        if (m_rpmVal) m_rpmVal->setText("800");
    }
    if (m_tempSlider) {
        QSignalBlocker b(m_tempSlider);
        m_tempSlider->setValue(21);
        if (m_tempVal) m_tempVal->setText("21 °C");
    }
    if (m_cl58Slider) {
        QSignalBlocker b(m_cl58Slider);
        m_cl58Slider->setValue(0);
        updateCl58Visual(0);
    }
    if (m_lightSlider) {
        QSignalBlocker b(m_lightSlider);
        m_lightSlider->setValue(128);
        updateLightVisual(128);
    }
    if (m_battSlider) {
        QSignalBlocker b(m_battSlider);
        m_battSlider->setValue(126);
        updateBatteryVisual(126);
    }
}

void VehicleTab::setIgnitionVisual(int level) {
    for (int i = 0; i < 3; ++i) {
        if (m_ignBtns[i]) m_ignBtns[i]->setChecked(i == level);
    }
}

void VehicleTab::updateCl58Visual(int pct) {
    const bool on = pct > 0;
    if (m_cl58OffBtn) m_cl58OffBtn->setChecked(!on);
    if (m_cl58OnBtn) m_cl58OnBtn->setChecked(on);
    if (m_cl58Val) {
        m_cl58Val->setText(on ? QString("ON %1%").arg(pct) : QString("OFF"));
    }
}

void VehicleTab::updateLightVisual(int val) {
    if (!m_lightVal) return;
    const char *tag = (val < 50) ? "Dark" : ((val < 180) ? "Day" : "Bright");
    m_lightVal->setText(QString("%1 (%2)").arg(val).arg(tag));
}

void VehicleTab::updateBatteryVisual(int dv) {
    if (!m_battVal) return;
    const QString vStr = QString("%1.%2 V").arg(dv / 10).arg(dv % 10);
    if (dv <= 105) {
        m_battVal->setText(vStr + " !");
        m_battVal->setStyleSheet("color: #f85149; font-weight: bold;");
    } else if (dv <= 115) {
        m_battVal->setText(vStr + " ⚠");
        m_battVal->setStyleSheet("color: #d29922; font-weight: bold;");
    } else {
        m_battVal->setText(vStr);
        m_battVal->setStyleSheet("");
    }
}
