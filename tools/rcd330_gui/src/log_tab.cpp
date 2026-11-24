#include "log_tab.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

LogTab::LogTab(VipClient *vip, QWidget *parent) : QWidget(parent), m_vip(vip) {
    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(10, 10, 10, 10);
    vbox->setSpacing(8);

    m_view = new QPlainTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setMaximumBlockCount(300);
    m_view->setObjectName("vipLog");
    vbox->addWidget(m_view, 1);

    auto *row = new QHBoxLayout();
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText("Raw VIP command (e.g. PING, IGN 2, OPS 40 30 30 40)...");
    auto *sendBtn = new QPushButton("Send", this);
    auto *clearBtn = new QPushButton("Clear", this);
    connect(sendBtn, &QPushButton::clicked, this, &LogTab::onSendRaw);
    connect(m_input, &QLineEdit::returnPressed, this, &LogTab::onSendRaw);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_view->clear();
        if (m_vip) m_vip->clearLog();
    });

    row->addWidget(m_input, 1);
    row->addWidget(sendBtn);
    row->addWidget(clearBtn);
    vbox->addLayout(row);

    if (m_vip) {
        for (const QString &l : m_vip->logLines()) {
            m_view->appendPlainText(l);
        }
        connect(m_vip, &VipClient::logAppended, this, &LogTab::onLogAppended);
    }
}

void LogTab::onLogAppended(const QString &line) {
    m_view->appendPlainText(line);
}

void LogTab::onSendRaw() {
    const QString cmd = m_input->text().trimmed();
    if (cmd.isEmpty() || !m_vip) return;
    m_vip->sendCommand(cmd);
    m_input->clear();
}
