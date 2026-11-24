#include "bezel_button.h"

#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QHideEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

BezelButton::BezelButton(const QString &label, const QString &keyName, HoldMode mode, QWidget *parent)
    : QWidget(parent), m_label(label), m_keyName(keyName), m_mode(mode) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(40);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    if (m_mode == HoldMode::PowerKey) {
        setToolTip("Short press: Standby / Wake\nHold 10 s (or right-click): Hard Reboot");
    } else if (m_mode == HoldMode::StandardHardkey) {
        setToolTip("Click & hold for real long press (2s Long / 4s Service / 9s Eng)\nRight-click for quick-hold shortcuts");
    }

    m_repaintTimer.setInterval(50);
    connect(&m_repaintTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));

    m_scriptedTimer.setSingleShot(true);
    connect(&m_scriptedTimer, &QTimer::timeout, this, &BezelButton::finishHold);
}

BezelButton::~BezelButton() {
    cancelHold();
}

QSize BezelButton::sizeHint() const {
    return QSize(112, 46);
}

void BezelButton::startScriptedHold(int durationMs) {
    if (durationMs <= 0) return;
    if (m_state != HoldState::Idle) {
        finishHold();
    }
    m_state = HoldState::ScriptedHold;
    m_scriptedTargetMs = durationMs;
    m_elapsed.start();
    m_repaintTimer.start();
    m_scriptedTimer.start(durationMs);
    emit keyPressed(m_keyName);
    update();
}

void BezelButton::cancelHold() {
    if (m_state != HoldState::Idle) {
        finishHold();
    }
}

void BezelButton::finishHold() {
    if (m_state == HoldState::Idle) return;
    m_state = HoldState::Idle;
    m_scriptedTargetMs = 0;
    m_repaintTimer.stop();
    m_scriptedTimer.stop();
    emit keyReleased(m_keyName);
    update();
}

void BezelButton::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (m_state == HoldState::ScriptedHold) {
            // Left click cancels active scripted hold cleanly without re-pressing
            finishHold();
            event->accept();
            return;
        }
        if (m_state == HoldState::Idle) {
            setFocus(Qt::MouseFocusReason);
            m_state = HoldState::ManualHold;
            m_elapsed.start();
            m_repaintTimer.start();
            emit keyPressed(m_keyName);
            update();
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void BezelButton::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (m_state == HoldState::ManualHold) {
            finishHold();
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void BezelButton::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    if (m_mode == HoldMode::PowerKey) {
        connect(menu.addAction("Short Tap — Standby / Wake (100 ms)"), &QAction::triggered, this, [this]() {
            startScriptedHold(100);
        });
        connect(menu.addAction("Hard Reboot Hold (10.5 s)"), &QAction::triggered, this, [this]() {
            startScriptedHold(10500);
        });
    } else if (m_mode == HoldMode::StandardHardkey) {
        connect(menu.addAction("Short Tap (100 ms)"), &QAction::triggered, this, [this]() {
            startScriptedHold(100);
        });
        connect(menu.addAction("Long Press (2.5 s)"), &QAction::triggered, this, [this]() {
            startScriptedHold(2500);
        });
        connect(menu.addAction("Service Hold (4.5 s)"), &QAction::triggered, this, [this]() {
            startScriptedHold(4500);
        });
        connect(menu.addAction("Ultra-Long Hold (10.0 s)"), &QAction::triggered, this, [this]() {
            startScriptedHold(10000);
        });
    } else {
        connect(menu.addAction("Tap (100 ms)"), &QAction::triggered, this, [this]() {
            startScriptedHold(100);
        });
    }
    if (m_state != HoldState::Idle) {
        menu.addSeparator();
        connect(menu.addAction("Cancel Active Hold"), &QAction::triggered, this, &BezelButton::cancelHold);
    }
    menu.exec(event->globalPos());
}

void BezelButton::focusOutEvent(QFocusEvent *event) {
    if (m_state == HoldState::ManualHold) {
        finishHold();
    }
    QWidget::focusOutEvent(event);
}

void BezelButton::hideEvent(QHideEvent *event) {
    cancelHold();
    QWidget::hideEvent(event);
}

void BezelButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
    const bool active = (m_state != HoldState::Idle);
    const qint64 ms = active ? m_elapsed.elapsed() : 0;

    QColor bg(0x1f, 0x26, 0x33);
    QColor border(0x32, 0x3c, 0x4e);
    QColor accent(0x2a, 0x7f, 0xff);

    if (active) {
        bg = QColor(0x27, 0x30, 0x42);
        if (m_mode == HoldMode::PowerKey) {
            accent = (ms >= 10000) ? QColor(0xd9, 0x30, 0x25) : QColor(0xf0, 0x8c, 0x2e);
        } else if (m_mode == HoldMode::StandardHardkey) {
            if (ms >= 9000) accent = QColor(0xd9, 0x30, 0x25);      // Ultra-long / Eng
            else if (ms >= 4000) accent = QColor(0xe6, 0x5c, 0x2e); // Service / Ext-long
            else if (ms >= 2000) accent = QColor(0xf0, 0xb4, 0x29); // Long
        }
        border = accent;
    }

    p.setPen(QPen(border, active ? 1.8 : 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(r, 7.0, 7.0);

    // Bottom progress fill bar while held
    if (active && m_mode != HoldMode::InstantKey) {
        const double maxMs = (m_state == HoldState::ScriptedHold && m_scriptedTargetMs > 0)
                                 ? static_cast<double>(m_scriptedTargetMs)
                                 : (m_mode == HoldMode::PowerKey ? 10000.0 : 9000.0);
        const double frac = std::clamp(static_cast<double>(ms) / maxMs, 0.0, 1.0);
        QRectF bar(r.left() + 4.0, r.bottom() - 5.0, (r.width() - 8.0) * frac, 3.5);
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawRoundedRect(bar, 1.5, 1.5);
    }

    QFont f = font();
    f.setBold(true);
    f.setPointSize(11);
    p.setFont(f);
    p.setPen(QColor(0xe6, 0xed, 0xf3));

    if (active && m_mode != HoldMode::InstantKey && ms >= 200) {
        const double sec = static_cast<double>(ms) / 1000.0;
        QString tag;
        if (m_mode == HoldMode::PowerKey) {
            tag = (ms >= 10000) ? "REBOOT" : QString("%1s").arg(sec, 0, 'f', 1);
        } else {
            if (ms >= 9000) tag = QString("%1s ENG").arg(sec, 0, 'f', 1);
            else if (ms >= 4000) tag = QString("%1s SVC").arg(sec, 0, 'f', 1);
            else if (ms >= 2000) tag = QString("%1s LNG").arg(sec, 0, 'f', 1);
            else tag = QString("%1s").arg(sec, 0, 'f', 1);
        }
        p.drawText(r.adjusted(4, 2, -4, -14), Qt::AlignCenter, m_label);
        f.setPointSize(9);
        p.setFont(f);
        p.setPen(accent.lighter(120));
        p.drawText(r.adjusted(4, 18, -4, -6), Qt::AlignCenter, tag);
    } else {
        p.drawText(r, Qt::AlignCenter, m_label);
    }
}
