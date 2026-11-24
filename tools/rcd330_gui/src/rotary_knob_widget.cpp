#include "rotary_knob_widget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

RotaryKnobWidget::RotaryKnobWidget(const QString &title,
                                   const QString &decLabel,
                                   const QString &centerLabel,
                                   const QString &centerKey,
                                   BezelButton::HoldMode centerMode,
                                   const QString &incLabel,
                                   QWidget *parent)
    : QWidget(parent) {
    setObjectName("knobCard");
    setToolTip("Scroll mouse wheel to turn knob; click/hold center button");

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(6, 6, 6, 6);
    vbox->setSpacing(6);

    auto *lbl = new QLabel(title, this);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setObjectName("knobTitle");
    vbox->addWidget(lbl);

    auto *row = new QHBoxLayout();
    row->setSpacing(4);

    auto *decBtn = new QPushButton(decLabel, this);
    decBtn->setFixedSize(30, 38);
    decBtn->setAutoRepeat(true);
    decBtn->setAutoRepeatDelay(300);
    decBtn->setAutoRepeatInterval(100);
    connect(decBtn, &QPushButton::clicked, this, [this]() { emit stepped(-1); });

    m_centerBtn = new BezelButton(centerLabel, centerKey, centerMode, this);
    m_centerBtn->setMinimumWidth(48);
    m_centerBtn->setFixedHeight(38);

    auto *incBtn = new QPushButton(incLabel, this);
    incBtn->setFixedSize(30, 38);
    incBtn->setAutoRepeat(true);
    incBtn->setAutoRepeatDelay(300);
    incBtn->setAutoRepeatInterval(100);
    connect(incBtn, &QPushButton::clicked, this, [this]() { emit stepped(1); });

    row->addWidget(decBtn);
    row->addWidget(m_centerBtn, 1);
    row->addWidget(incBtn);
    vbox->addLayout(row);
}

void RotaryKnobWidget::wheelEvent(QWheelEvent *event) {
    const int dy = event->angleDelta().y();
    if (dy != 0) {
        emit stepped(dy > 0 ? 1 : -1);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}
