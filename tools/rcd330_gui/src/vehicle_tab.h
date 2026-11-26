#pragma once

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QWidget>

class VehicleTab : public QWidget {
    Q_OBJECT

public:
    explicit VehicleTab(QWidget *parent = nullptr);
    void resetForReboot();

signals:
    void commandRequested(const QString &cmd);
    void buttonTapRequested(const QString &keyName);

private:
    void setIgnitionVisual(int level);
    void updateCl58Visual(int pct);
    void updateLightVisual(int val);
    void updateBatteryVisual(int dv);

    QPushButton *m_ignBtns[3] = {nullptr, nullptr, nullptr};
    QPushButton *m_cl58OffBtn = nullptr;
    QPushButton *m_cl58OnBtn = nullptr;
    QSlider *m_speedSlider = nullptr;
    QLabel *m_speedVal = nullptr;
    QSlider *m_rpmSlider = nullptr;
    QLabel *m_rpmVal = nullptr;
    QSlider *m_tempSlider = nullptr;
    QLabel *m_tempVal = nullptr;
    QSlider *m_cl58Slider = nullptr;
    QLabel *m_cl58Val = nullptr;
    QSlider *m_lightSlider = nullptr;
    QLabel *m_lightVal = nullptr;
    QSlider *m_battSlider = nullptr;
    QLabel *m_battVal = nullptr;
    QPushButton *m_handbrakeBtn = nullptr;
    QComboBox *m_canLangCombo = nullptr;
};
