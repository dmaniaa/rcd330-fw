#include "coding_tab.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QPushButton>
#include <QVBoxLayout>

CodingTab::CodingTab(const CodingBridge &bridge, VipClient *vip, QWidget *parent)
    : QWidget(parent), m_bridge(bridge), m_vip(vip) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 8);
    outer->setSpacing(8);

    auto *cols = new QHBoxLayout();
    cols->setSpacing(16);

    // Left column: Platform / Identity / Vehicle / OPS
    auto *leftBox = new QVBoxLayout();
    leftBox->setSpacing(6);

    auto makeRow = [&](const QString &lbl, QWidget *field, QWidget **rowContainerOut = nullptr) {
        auto *w = new QWidget(this);
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        auto *l = new QLabel(lbl, w);
        l->setMinimumWidth(130);
        h->addWidget(l);
        h->addWidget(field, 1);
        leftBox->addWidget(w);
        if (rowContainerOut) *rowContainerOut = w;
    };

    auto *platWrap = new QWidget(this);
    auto *platRow = new QHBoxLayout(platWrap);
    platRow->setContentsMargins(0, 0, 0, 0);
    platRow->setSpacing(6);
    m_platform = new QComboBox(platWrap);
    m_platform->setObjectName("platformCombo");
    m_platform->addItem("PQ (7-byte)", "PQ");
    m_platform->addItem("MQB (25-byte)", "MQB");
    m_brand = new QComboBox(platWrap);
    m_brand->addItem("VW", "VW");
    m_brand->addItem("Skoda", "SKODA");
    m_canSpeed = new QComboBox(platWrap);
    m_canSpeed->addItem("CAN HS (500k)", "HS");
    m_canSpeed->addItem("CAN LS (100k)", "LS");
    platRow->addWidget(m_platform);
    platRow->addWidget(m_brand);
    platRow->addWidget(m_canSpeed);
    makeRow("Platform / Brand", platWrap);

    m_partNum = new QLineEdit("6RD035187B ", this);
    m_partNum->setMaxLength(11);
    makeRow("Part Number (11B)", m_partNum);

    auto *opsWrap = new QWidget(this);
    auto *opsRow = new QHBoxLayout(opsWrap);
    opsRow->setContentsMargins(0, 0, 0, 0);
    opsRow->setSpacing(6);
    m_ops = new QComboBox(opsWrap);
    m_ops->addItem("OPS Off", "OFF");
    m_ops->addItem("OPS 4K (Rear)", "4K");
    m_ops->addItem("OPS 8K (F+R)", "8K");
    m_ops->addItem("OPS 360°", "360");
    m_speakers = new QComboBox(opsWrap);
    m_speakers->setObjectName("speakersCombo");
    m_speakers->addItem("4 Speakers (Fader+Bal)", 4);
    m_speakers->addItem("2 Speakers (Bal only)", 2);
    opsRow->addWidget(m_ops);
    opsRow->addWidget(m_speakers);
    makeRow("OPS / Speakers", opsWrap);

    m_vehPq = new QComboBox(this);
    makeRow("PQ Vehicle (B6)", m_vehPq, &m_rowVehPq);
    m_vehMqb = new QComboBox(this);
    makeRow("MQB Vehicle (VRM)", m_vehMqb, &m_rowVehMqb);
    m_bootlogo = new QComboBox(this);
    makeRow("MQB Bootlogo (B18)", m_bootlogo, &m_rowBootlogo);
    leftBox->addStretch(1);

    cols->addLayout(leftBox, 1);

    // Right column: Feature checkboxes + Raw Hex + Actions
    auto *rightBox = new QVBoxLayout();
    rightBox->setSpacing(6);

    auto *chkGrid = new QGridLayout();
    chkGrid->setHorizontalSpacing(12);
    chkGrid->setVerticalSpacing(4);
    m_rvc = new QCheckBox("Rear View Camera (RVC)", this);
    m_bt = new QCheckBox("Bluetooth Enabled", this);
    m_eng = new QCheckBox("Dev / Eng Menu (Ch 50)", this);
    m_extAmp = new QCheckBox("External Amp (B1=02)", this);
    m_extMic = new QCheckBox("External Mic R38 (B3.1)", this);
    m_carplay = new QCheckBox("CarPlay (B0.2)", this);
    m_aa = new QCheckBox("Android Auto (B0.3)", this);
    m_mlink = new QCheckBox("MirrorLink (B0.1)", this);
    m_carlife = new QCheckBox("CarLife (B0.4)", this);
    m_amDis = new QCheckBox("Disable AM Band (B14.1)", this);
    m_dLock = new QCheckBox("Driving Lockout (B23.0)", this);

    QCheckBox *allChecks[] = {
        m_rvc, m_bt, m_eng, m_extAmp, m_extMic, m_carplay,
        m_aa, m_mlink, m_carlife, m_amDis, m_dLock
    };
    for (int i = 0; i < 11; ++i) {
        chkGrid->addWidget(allChecks[i], i / 2, i % 2);
    }
    rightBox->addLayout(chkGrid);

    auto *hexRow = new QHBoxLayout();
    auto *hexLbl = new QLabel("Long Coding Hex", this);
    hexLbl->setMinimumWidth(115);
    m_hexEdit = new QLineEdit(this);
    m_hexEdit->setObjectName("hexInput");
    m_hexEdit->setPlaceholderText("0E000440040013");
    hexRow->addWidget(hexLbl);
    hexRow->addWidget(m_hexEdit, 1);
    rightBox->addLayout(hexRow);

    auto *btnRow = new QHBoxLayout();
    auto *saveBtn = new QPushButton("Save & Apply Live", this);
    saveBtn->setObjectName("primaryBtn");
    auto *rawBtn = new QPushButton("Apply Raw Hex", this);
    rawBtn->setObjectName("applyRawHexBtn");
    auto *resetBtn = new QPushButton("Reset Defaults", this);
    connect(saveBtn, &QPushButton::clicked, this, &CodingTab::onSaveStructured);
    connect(rawBtn, &QPushButton::clicked, this, &CodingTab::onApplyRawHex);
    connect(resetBtn, &QPushButton::clicked, this, &CodingTab::onResetDefaults);
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(rawBtn);
    btnRow->addWidget(resetBtn);
    btnRow->addStretch(1);
    rightBox->addLayout(btnRow);

    m_warnLabel = new QLabel(this);
    m_warnLabel->setObjectName("warnText");
    m_warnLabel->setWordWrap(true);
    rightBox->addWidget(m_warnLabel);
    rightBox->addStretch(1);

    cols->addLayout(rightBox, 1);
    outer->addLayout(cols);

    auto *metaRow = new QHBoxLayout();
    m_fileLabel = new QLabel("Storage: loading...", this);
    m_fileLabel->setObjectName("mutedText");
    m_msgLabel = new QLabel(this);
    m_msgLabel->setObjectName("mutedText");
    metaRow->addWidget(m_fileLabel, 1);
    metaRow->addWidget(m_msgLabel);
    outer->addLayout(metaRow);

    connect(m_platform, &QComboBox::currentIndexChanged, this, &CodingTab::onPlatformChanged);
    reloadFromDisk();
}

void CodingTab::reloadFromDisk() {
    CodingPayload p;
    QString err;
    if (!m_bridge.load(p, err)) {
        m_warnLabel->setText("Failed to load coding: " + err);
        return;
    }
    renderPayload(p);
}

void CodingTab::syncLoadedCodingToVip() {
    if (!m_vip) return;
    CodingPayload p;
    QString err;
    if (m_bridge.load(p, err)) {
        m_current = p;
        m_vip->sendCommands(p.vipCmds);
    }
}

void CodingTab::renderPayload(const CodingPayload &p) {
    m_current = p;

    auto populate = [](QComboBox *box, const QMap<int, QString> &items, bool hexKeys) {
        box->blockSignals(true);
        box->clear();
        for (auto it = items.begin(); it != items.end(); ++it) {
            const QString prefix = hexKeys
                                       ? QString("0x%1").arg(it.key(), 2, 16, QChar('0')).toUpper()
                                       : QString::number(it.key());
            box->addItem(QString("%1 — %2").arg(prefix, it.value()), it.key());
        }
        box->blockSignals(false);
    };

    populate(m_vehPq, p.pqVehicles, false);
    populate(m_vehMqb, p.mqbVehicles, true);
    populate(m_bootlogo, p.mqbBootlogos, true);

    auto selectByData = [](QComboBox *box, const QVariant &val) {
        const int idx = box->findData(val);
        if (idx >= 0) box->setCurrentIndex(idx);
    };

    m_platform->blockSignals(true);
    selectByData(m_platform, p.platform);
    m_platform->blockSignals(false);
    selectByData(m_brand, p.brand);
    selectByData(m_canSpeed, p.canSpeed);
    m_partNum->setText(p.partNumber);
    selectByData(m_ops, p.opsMode);
    selectByData(m_speakers, p.speakers);
    selectByData(m_vehPq, p.vehiclePq);
    selectByData(m_vehMqb, p.vehicleMqb);
    selectByData(m_bootlogo, p.bootlogoId);

    m_rvc->setChecked(p.rvcEnabled);
    m_bt->setChecked(p.btEnabled);
    m_eng->setChecked(p.engMode);
    m_extAmp->setChecked(p.extAmpEnabled);
    m_extMic->setChecked(p.extMicEnabled);
    m_carplay->setChecked(p.carplayEnabled);
    m_aa->setChecked(p.androidAutoEnabled);
    m_mlink->setChecked(p.mirrorlinkEnabled);
    m_carlife->setChecked(p.carlifeEnabled);
    m_amDis->setChecked(p.amDisabled);
    m_dLock->setChecked(p.drivingLock);

    m_hexEdit->setText(p.codingHex);
    m_warnLabel->setText(p.validationWarnings.join('\n'));
    m_fileLabel->setText("Persisted in: " + p.codingFile + " (pre-seeded into /pss1 & /pss2 on next boot)");
    updatePlatformVisibility(p.platform);
}

void CodingTab::updatePlatformVisibility(const QString &plat) {
    const bool isPq = (plat == "PQ");
    m_rowVehPq->setVisible(isPq);
    m_rowVehMqb->setVisible(!isPq);
    m_rowBootlogo->setVisible(!isPq);
    m_extAmp->setVisible(isPq);
    m_extMic->setVisible(isPq);
    m_carplay->setVisible(isPq);
    m_aa->setVisible(isPq);
    m_mlink->setVisible(isPq);
    m_carlife->setVisible(isPq);
    m_amDis->setVisible(!isPq);
    m_dLock->setVisible(!isPq);
}

void CodingTab::onPlatformChanged() {
    updatePlatformVisibility(currentPlatform());
}

QString CodingTab::currentPlatform() const {
    return m_platform->currentData().toString();
}

bool CodingTab::isPqVehicleRowVisible() const {
    return !m_rowVehPq->isHidden();
}

bool CodingTab::isMqbVehicleRowVisible() const {
    return !m_rowVehMqb->isHidden();
}

void CodingTab::onSaveStructured() { saveWithMode(false); }
void CodingTab::onApplyRawHex() { saveWithMode(true); }

void CodingTab::saveWithMode(bool rawHexOnly) {
    const QString plat = currentPlatform();
    QJsonObject body;
    body["platform"] = plat;
    body["brand"] = m_brand->currentData().toString();
    body["can_speed"] = m_canSpeed->currentData().toString();
    body["part_number"] = m_partNum->text();
    body["ops_mode"] = m_ops->currentData().toString();
    body["eng_mode"] = m_eng->isChecked();
    body["vehicle_mqb"] = m_vehMqb->currentData().toInt();

    if (rawHexOnly) {
        body["coding_hex"] = m_hexEdit->text().trimmed();
    } else {
        body["speakers"] = m_speakers->currentData().toInt();
        body["rvc_enabled"] = m_rvc->isChecked();
        body["bt_enabled"] = m_bt->isChecked();
        if (plat == "PQ") {
            body["ext_amp_enabled"] = m_extAmp->isChecked();
            body["ext_mic_enabled"] = m_extMic->isChecked();
            body["carplay_enabled"] = m_carplay->isChecked();
            body["android_auto_enabled"] = m_aa->isChecked();
            body["mirrorlink_enabled"] = m_mlink->isChecked();
            body["carlife_enabled"] = m_carlife->isChecked();
            body["vehicle_pq"] = m_vehPq->currentData().toInt();
        } else {
            body["am_disabled"] = m_amDis->isChecked();
            body["driving_lock"] = m_dLock->isChecked();
            body["bootlogo_id"] = m_bootlogo->currentData().toInt();
        }
    }

    CodingPayload updated;
    QString err;
    if (!m_bridge.apply(body, true, updated, err)) {
        m_warnLabel->setText("Save failed: " + err);
        return;
    }
    renderPayload(updated);
    if (m_vip && m_vip->isConnected()) {
        m_vip->sendCommands(updated.vipCmds, [this](const QStringList &replies) {
            m_msgLabel->setText("Saved & sent to VIP (" + replies.join(", ") + ")");
        });
    } else {
        m_msgLabel->setText("Saved locally (VIP offline — will sync on connect)");
    }
}

void CodingTab::onResetDefaults() {
    CodingPayload p;
    QString err;
    if (!m_bridge.reset(true, p, err)) {
        m_warnLabel->setText("Reset failed: " + err);
        return;
    }
    renderPayload(p);
    if (m_vip && m_vip->isConnected()) {
        m_vip->sendCommands(p.vipCmds, [this](const QStringList &replies) {
            m_msgLabel->setText("Reset to defaults & sent to VIP (" + replies.join(", ") + ")");
        });
    } else {
        m_msgLabel->setText("Reset locally (VIP offline — will sync on connect)");
    }
}
