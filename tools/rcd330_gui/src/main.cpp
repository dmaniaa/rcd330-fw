#include "main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>

static const char *kDarkStyle = R"(
QMainWindow, QWidget {
    background-color: #0f131a;
    color: #e6edf3;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    font-size: 12px;
}
QFrame#faceplate {
    background-color: #151a22;
    border: 1px solid #262e3d;
    border-radius: 12px;
}
QWidget#knobCard {
    background-color: #10151d;
    border: 1px solid #262e3d;
    border-radius: 9px;
}
QLabel#knobTitle {
    color: #8b97a8;
    font-size: 10px;
    font-weight: 700;
}
QLabel#statusBadge {
    background-color: #10151d;
    border: 1px solid #262e3d;
    border-radius: 10px;
    padding: 4px 10px;
    color: #9bb1cc;
    font-size: 11px;
}
QLabel#warnText {
    color: #f0b429;
    font-size: 11px;
}
QLabel#mutedText {
    color: #8b97a8;
    font-size: 11px;
}
QTabWidget::pane {
    border: 1px solid #262e3d;
    border-radius: 8px;
    background-color: #151a22;
}
QTabBar::tab {
    background-color: #19202b;
    color: #9bb1cc;
    border: 1px solid #262e3d;
    border-bottom: none;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    padding: 6px 14px;
    margin-right: 4px;
    font-weight: 600;
}
QTabBar::tab:selected {
    background-color: #243043;
    color: #ffffff;
}
QGroupBox {
    border: 1px solid #262e3d;
    border-radius: 8px;
    margin-top: 10px;
    padding-top: 8px;
    font-weight: 700;
    color: #8b97a8;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
}
QPushButton {
    background-color: #1f2633;
    color: #e6edf3;
    border: 1px solid #323c4e;
    border-radius: 6px;
    padding: 6px 10px;
    font-weight: 600;
}
QPushButton:hover {
    background-color: #293345;
}
QPushButton:pressed {
    background-color: #161c26;
}
QPushButton:checked, QPushButton#primaryBtn {
    background-color: #1d5fd6;
    border-color: #2a7fff;
    color: #ffffff;
}
QPushButton#warnBtn {
    background-color: #4a2323;
    border-color: #7c3535;
}
QComboBox, QLineEdit {
    background-color: #10151d;
    color: #e6edf3;
    border: 1px solid #323c4e;
    border-radius: 5px;
    padding: 4px 8px;
}
QLineEdit#hexInput {
    font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    letter-spacing: 1px;
}
QPlainTextEdit#vipLog {
    background-color: #0b0e13;
    color: #9be09b;
    border: 1px solid #222937;
    border-radius: 6px;
    font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    font-size: 11px;
}
)";

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("rcd330_gui");
    app.setStyleSheet(kDarkStyle);

    QCommandLineParser parser;
    parser.setApplicationDescription("VW RCD 330 Native Control Panel & Display");
    parser.addHelpOption();

    QCommandLineOption vipHostOpt("vip-host", "VIP TCP host", "host", "127.0.0.1");
    QCommandLineOption vipPortOpt("vip-port", "VIP TCP port", "port", "4444");
    QCommandLineOption vncHostOpt("vnc-host", "QEMU VNC host", "host", "127.0.0.1");
    QCommandLineOption vncPortOpt("vnc-port", "QEMU VNC port", "port", "5900");
    QCommandLineOption fwNameOpt("fw-name", "Firmware display name", "name", "RCD 330");
    QCommandLineOption codingFileOpt("coding-file", "Persistent coding JSON path", "path", "");
    QCommandLineOption codingScriptOpt("coding-script", "Path to rcd330_coding.py", "path", "");
    QCommandLineOption qemuPidOpt("qemu-pid", "QEMU process PID to monitor", "pid", "0");

    parser.addOptions({vipHostOpt, vipPortOpt, vncHostOpt, vncPortOpt,
                       fwNameOpt, codingFileOpt, codingScriptOpt, qemuPidOpt});
    parser.process(app);

    GuiConfig cfg;
    cfg.vipHost = parser.value(vipHostOpt);
    cfg.vipPort = static_cast<quint16>(parser.value(vipPortOpt).toUInt());
    cfg.vncHost = parser.value(vncHostOpt);
    cfg.vncPort = static_cast<quint16>(parser.value(vncPortOpt).toUInt());
    cfg.fwName = parser.value(fwNameOpt);
    cfg.codingFile = parser.value(codingFileOpt);
    cfg.codingScript = parser.value(codingScriptOpt);
    cfg.qemuPid = parser.value(qemuPidOpt).toLongLong();

    if (cfg.codingScript.isEmpty()) {
        const QDir appDir(QCoreApplication::applicationDirPath());
        cfg.codingScript = QFileInfo(appDir.filePath("../../tools/rcd330_coding.py")).absoluteFilePath();
    }
    if (cfg.codingFile.isEmpty()) {
        const QDir appDir(QCoreApplication::applicationDirPath());
        QString safeFw = cfg.fwName;
        for (QChar &ch : safeFw) {
            if (!ch.isLetterOrNumber() && ch != '.' && ch != '_' && ch != '-') ch = '_';
        }
        cfg.codingFile = QFileInfo(appDir.filePath("../coding/" + safeFw + ".json")).absoluteFilePath();
    }

    MainWindow win(cfg);
    win.show();
    win.raise();
    win.activateWindow();
    return app.exec();
}
