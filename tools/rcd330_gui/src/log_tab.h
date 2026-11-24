#pragma once

#include "vip_client.h"
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QWidget>

class LogTab : public QWidget {
    Q_OBJECT

public:
    explicit LogTab(VipClient *vip, QWidget *parent = nullptr);

private slots:
    void onLogAppended(const QString &line);
    void onSendRaw();

private:
    VipClient *m_vip = nullptr;
    QPlainTextEdit *m_view = nullptr;
    QLineEdit *m_input = nullptr;
};
