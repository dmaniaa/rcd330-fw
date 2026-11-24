#pragma once

#include "coding_bridge.h"
#include "vip_client.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>

class CodingTab : public QWidget {
    Q_OBJECT

public:
    CodingTab(const CodingBridge &bridge, VipClient *vip, QWidget *parent = nullptr);

    void reloadFromDisk();
    void syncLoadedCodingToVip();

    QString currentPlatform() const;
    bool isPqVehicleRowVisible() const;
    bool isMqbVehicleRowVisible() const;
    QString statusMessage() const { return m_msgLabel ? m_msgLabel->text() : QString(); }

private slots:
    void onPlatformChanged();
    void onSaveStructured();
    void onApplyRawHex();
    void onResetDefaults();

private:
    void renderPayload(const CodingPayload &p);
    void saveWithMode(bool rawHexOnly);
    void updatePlatformVisibility(const QString &plat);

    CodingBridge m_bridge;
    VipClient *m_vip = nullptr;
    CodingPayload m_current;

    QComboBox *m_platform = nullptr;
    QComboBox *m_brand = nullptr;
    QComboBox *m_canSpeed = nullptr;
    QLineEdit *m_partNum = nullptr;
    QComboBox *m_ops = nullptr;
    QComboBox *m_speakers = nullptr;
    QComboBox *m_vehPq = nullptr;
    QComboBox *m_vehMqb = nullptr;
    QComboBox *m_bootlogo = nullptr;

    QWidget *m_rowVehPq = nullptr;
    QWidget *m_rowVehMqb = nullptr;
    QWidget *m_rowBootlogo = nullptr;

    QCheckBox *m_rvc = nullptr;
    QCheckBox *m_bt = nullptr;
    QCheckBox *m_eng = nullptr;
    QCheckBox *m_extAmp = nullptr;
    QCheckBox *m_extMic = nullptr;
    QCheckBox *m_carplay = nullptr;
    QCheckBox *m_aa = nullptr;
    QCheckBox *m_mlink = nullptr;
    QCheckBox *m_carlife = nullptr;
    QCheckBox *m_amDis = nullptr;
    QCheckBox *m_dLock = nullptr;

    QLineEdit *m_hexEdit = nullptr;
    QLabel *m_warnLabel = nullptr;
    QLabel *m_fileLabel = nullptr;
    QLabel *m_msgLabel = nullptr;
};
