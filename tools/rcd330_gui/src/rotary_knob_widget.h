#pragma once

#include "bezel_button.h"
#include <QWidget>

class RotaryKnobWidget : public QWidget {
    Q_OBJECT

public:
    RotaryKnobWidget(const QString &title,
                     const QString &decLabel,
                     const QString &centerLabel,
                     const QString &centerKey,
                     BezelButton::HoldMode centerMode,
                     const QString &incLabel,
                     QWidget *parent = nullptr);

    BezelButton *centerButton() const { return m_centerBtn; }

signals:
    void stepped(int delta);

protected:
    void wheelEvent(QWheelEvent *event) override;

private:
    BezelButton *m_centerBtn = nullptr;
};
