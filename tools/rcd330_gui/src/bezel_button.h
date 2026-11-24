#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QTimer>
#include <QWidget>

class BezelButton : public QWidget {
    Q_OBJECT

public:
    enum class HoldMode {
        StandardHardkey,
        PowerKey,
        InstantKey,
    };

    BezelButton(const QString &label,
                const QString &keyName,
                HoldMode mode = HoldMode::StandardHardkey,
                QWidget *parent = nullptr);
    ~BezelButton() override;

    void startScriptedHold(int durationMs);
    void cancelHold();
    bool isHeld() const { return m_state != HoldState::Idle; }
    QString keyName() const { return m_keyName; }

signals:
    void keyPressed(const QString &keyName);
    void keyReleased(const QString &keyName);

protected:
    QSize sizeHint() const override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    enum class HoldState { Idle, ManualHold, ScriptedHold };

    void finishHold();

    QString m_label;
    QString m_keyName;
    HoldMode m_mode;
    HoldState m_state = HoldState::Idle;
    int m_scriptedTargetMs = 0;
    QElapsedTimer m_elapsed;
    QTimer m_repaintTimer;
    QTimer m_scriptedTimer;
};
