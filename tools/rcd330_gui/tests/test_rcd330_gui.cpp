#include <QtTest/QtTest>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFocusEvent>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtEndian>
#include <sys/wait.h>
#include <unistd.h>
#include "bezel_button.h"
#include "coding_bridge.h"
#include "main_window.h"
#include "rfb_protocol.h"
#include "rfb_widget.h"
#include "rotary_knob_widget.h"
#include "vip_client.h"

static void appendU16BE(QByteArray &buf, quint16 v) {
    char b[2];
    qToBigEndian(v, b);
    buf.append(b, 2);
}

static void appendU32BE(QByteArray &buf, quint32 v) {
    char b[4];
    qToBigEndian(v, b);
    buf.append(b, 4);
}

static void appendS32BE(QByteArray &buf, qint32 v) {
    appendU32BE(buf, static_cast<quint32>(v));
}

class TestRcd330Gui : public QObject {
    Q_OBJECT

private slots:
    void testRfbHandshakeFragmentedRawAndCopyRect() {
        RfbSession s;
        QByteArray out;
        bool dirty = false;
        QString err;

        QVERIFY(s.feed("RFB 003.008\n", out, dirty, err));
        QCOMPARE(out, QByteArray("RFB 003.008\n"));
        QCOMPARE(s.state(), RfbSession::State::AwaitingSecurityTypes);

        out.clear();
        QByteArray secTypes;
        secTypes.append(char(1));
        secTypes.append(char(1));
        QVERIFY(s.feed(secTypes, out, dirty, err));
        QCOMPARE(out, QByteArray(1, char(1)));
        QCOMPARE(s.state(), RfbSession::State::AwaitingSecurityResult);

        out.clear();
        QVERIFY(s.feed(QByteArray(4, '\0'), out, dirty, err));
        QCOMPARE(out, QByteArray(1, char(1)));
        QCOMPARE(s.state(), RfbSession::State::AwaitingServerInit);

        QByteArray init;
        appendU16BE(init, 8);
        appendU16BE(init, 4);
        init.append(QByteArray(16, '\0'));
        appendU32BE(init, 4);
        init.append("QEMU");
        out.clear();
        QVERIFY(s.feed(init, out, dirty, err));
        QCOMPARE(s.state(), RfbSession::State::Ready);
        QCOMPARE(s.framebuffer().size(), QSize(8, 4));
        QCOMPARE(out.size(), 46);
        QCOMPARE(quint8(out[0]), quint8(0));
        QCOMPARE(quint8(out[20]), quint8(2));
        QCOMPARE(quint8(out[36]), quint8(3));

        QByteArray fbu;
        fbu.append(char(0));
        fbu.append(char(0));
        appendU16BE(fbu, 2);
        appendU16BE(fbu, 0); appendU16BE(fbu, 0);
        appendU16BE(fbu, 2); appendU16BE(fbu, 2);
        appendS32BE(fbu, 0);
        const quint32 redLe = qToLittleEndian<quint32>(0x00FF0000u);
        for (int i = 0; i < 4; ++i) {
            fbu.append(reinterpret_cast<const char *>(&redLe), 4);
        }
        appendU16BE(fbu, 1); appendU16BE(fbu, 1);
        appendU16BE(fbu, 2); appendU16BE(fbu, 2);
        appendS32BE(fbu, 1);
        appendU16BE(fbu, 0); appendU16BE(fbu, 0);

        bool sawDirty = false;
        out.clear();
        for (int i = 0; i < fbu.size(); i += 3) {
            QByteArray chunkOut;
            QVERIFY2(s.feed(fbu.mid(i, 3), chunkOut, dirty, err), qPrintable(err));
            out.append(chunkOut);
            sawDirty = sawDirty || dirty;
        }
        QVERIFY(sawDirty);
        QCOMPARE(out.size(), 10);
        QCOMPARE(quint8(out[0]), quint8(3));
        QCOMPARE(quint8(out[1]), quint8(1));
        QCOMPARE(qRed(s.framebuffer().pixel(0, 0)), 255);
        QCOMPARE(qRed(s.framebuffer().pixel(2, 2)), 255);
        QCOMPARE(qRed(s.framebuffer().pixel(3, 3)), 0);

        QByteArray dsz;
        dsz.append(char(0)); dsz.append(char(0));
        appendU16BE(dsz, 1);
        appendU16BE(dsz, 0); appendU16BE(dsz, 0);
        appendU16BE(dsz, 800); appendU16BE(dsz, 480);
        appendS32BE(dsz, -223);
        out.clear();
        QVERIFY(s.feed(dsz, out, dirty, err));
        QCOMPARE(s.framebuffer().size(), QSize(800, 480));
    }

    void testRfbPointerCoordinateClamping() {
        RfbScreenWidget w;
        w.resize(800, 480);
        QCOMPARE(w.mapWidgetToFramebuffer(QPointF(0, 0)), QPoint(0, 0));
        QCOMPARE(w.mapWidgetToFramebuffer(QPointF(799, 479)), QPoint(799, 479));
        QCOMPARE(w.mapWidgetToFramebuffer(QPointF(-50, -10)), QPoint(0, 0));
        QCOMPARE(w.mapWidgetToFramebuffer(QPointF(1200, 900)), QPoint(799, 479));

        w.resize(400, 240);
        QCOMPARE(w.mapWidgetToFramebuffer(QPointF(200, 120)), QPoint(400, 240));

        QByteArray ev = RfbSession::buildPointerEvent(1, 320, 240);
        QCOMPARE(ev.size(), 6);
        QCOMPARE(quint8(ev[0]), quint8(5));
        QCOMPARE(quint8(ev[1]), quint8(1));
        QCOMPARE(qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(ev.constData() + 2)), quint16(320));
        QCOMPARE(qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(ev.constData() + 4)), quint16(240));
    }

    void testVipClientProtocolHardening() {
        QTcpServer srv;
        QVERIFY(srv.listen(QHostAddress::LocalHost, 0));

        VipClient cli;
        QVERIFY(!cli.sendCommand("TUNER 9850 TEST\nRAW 1 1 2 3"));
        QVERIFY(!cli.sendCommand("PING\rOK"));
        QVERIFY(!cli.sendCommand("   "));

        QTcpSocket *serverSide = nullptr;
        connect(&srv, &QTcpServer::newConnection, &srv, [&]() {
            serverSide = srv.nextPendingConnection();
        });

        QString pingReply;
        bool sawReboot = false;
        connect(&cli, &VipClient::rebootObserved, &cli, [&]() { sawReboot = true; });
        QVERIFY(cli.sendCommand("PING", [&](const QString &r) { pingReply = r; }));

        cli.connectToServer("127.0.0.1", srv.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(serverSide != nullptr, 2000);

        QTRY_VERIFY_WITH_TIMEOUT(serverSide->bytesAvailable() >= 5, 2000);
        QCOMPARE(serverSide->readAll(), QByteArray("PING\n"));

        serverSide->write("HOST_TX 0x05 0x01 len=3\nREBOOT POWER_10S\nPONG rcd");
        serverSide->flush();
        QTest::qWait(30);
        QVERIFY(pingReply.isEmpty());
        QVERIFY(sawReboot);

        serverSide->write("330-vip\n");
        serverSide->flush();
        QTRY_COMPARE_WITH_TIMEOUT(pingReply, QString("PONG rcd330-vip"), 2000);
        QVERIFY(cli.isConnected());
    }

    void testCodingBridgeRoundTrip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString script = QDir(QStringLiteral(RCD330_REPO_ROOT)).filePath("tools/rcd330_coding.py");
        const QString codingFile = dir.filePath("coding.json");

        CodingBridge bridge(script, codingFile, "5624_VW");
        CodingPayload p;
        QString err;
        QVERIFY2(bridge.load(p, err), qPrintable(err));
        QCOMPARE(p.platform, QString("PQ"));
        QVERIFY(p.rvcEnabled);
        QVERIFY(p.pqVehicles.contains(19));

        QJsonObject upd;
        upd["rvc_enabled"] = false;
        upd["ops_mode"] = "360";
        upd["eng_mode"] = true;
        QVERIFY2(bridge.apply(upd, true, p, err), qPrintable(err));
        QVERIFY(!p.rvcEnabled);
        QCOMPARE(p.opsMode, QString("360"));
        QVERIFY(p.engMode);
        QVERIFY(QFileInfo::exists(codingFile));

        QVERIFY2(bridge.reset(true, p, err), qPrintable(err));
        QVERIFY(p.rvcEnabled);
        QCOMPARE(p.opsMode, QString("8K"));
        QVERIFY(!p.engMode);
    }

    void testBezelButtonLongPressAndSafetyGuards() {
        BezelButton btn("SETUP", "SETUP", BezelButton::HoldMode::StandardHardkey);
        btn.show();
        QStringList events;
        connect(&btn, &BezelButton::keyPressed, &btn, [&](const QString &k) { events << ("DOWN:" + k); });
        connect(&btn, &BezelButton::keyReleased, &btn, [&](const QString &k) { events << ("UP:" + k); });

        QTest::mousePress(&btn, Qt::LeftButton);
        QVERIFY(btn.isHeld());
        QCOMPARE(events, QStringList({"DOWN:SETUP"}));
        QTest::mouseRelease(&btn, Qt::LeftButton);
        QVERIFY(!btn.isHeld());
        QCOMPARE(events, QStringList({"DOWN:SETUP", "UP:SETUP"}));

        events.clear();
        btn.startScriptedHold(80);
        QVERIFY(btn.isHeld());
        QCOMPARE(events, QStringList({"DOWN:SETUP"}));
        QTRY_VERIFY_WITH_TIMEOUT(!btn.isHeld(), 1000);
        QCOMPARE(events, QStringList({"DOWN:SETUP", "UP:SETUP"}));

        events.clear();
        btn.startScriptedHold(5000);
        QCOMPARE(events, QStringList({"DOWN:SETUP"}));
        QTest::mousePress(&btn, Qt::LeftButton);
        QVERIFY(!btn.isHeld());
        QCOMPARE(events, QStringList({"DOWN:SETUP", "UP:SETUP"}));
        QTest::mouseRelease(&btn, Qt::LeftButton);
        QCOMPARE(events, QStringList({"DOWN:SETUP", "UP:SETUP"}));

        events.clear();
        QTest::mousePress(&btn, Qt::LeftButton);
        QVERIFY(btn.isHeld());
        btn.hide();
        QVERIFY(!btn.isHeld());
        QCOMPARE(events, QStringList({"DOWN:SETUP", "UP:SETUP"}));

        // Safety guard: focusOut releases active manual hold
        btn.show();
        events.clear();
        QTest::mousePress(&btn, Qt::LeftButton);
        QVERIFY(btn.isHeld());
        QFocusEvent focusOut(QEvent::FocusOut);
        QCoreApplication::sendEvent(&btn, &focusOut);
        QVERIFY(!btn.isHeld());
        QCOMPARE(events, QStringList({"DOWN:SETUP", "UP:SETUP"}));
    }

    void testMainWindowDockToggleAndCodingPlatformSwitch() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GuiConfig cfg;
        cfg.fwName = "5624_VW";
        cfg.codingFile = dir.filePath("mw_coding.json");
        cfg.codingScript = QDir(QStringLiteral(RCD330_REPO_ROOT)).filePath("tools/rcd330_coding.py");
        cfg.autoConnect = false;

        MainWindow win(cfg);
        win.show();
        QVERIFY(win.isDockVisible());
        win.toggleDock();
        QVERIFY(!win.isDockVisible());
        win.toggleDock();
        QVERIFY(win.isDockVisible());

        QVERIFY(win.codingTab() != nullptr);
        QCOMPARE(win.codingTab()->currentPlatform(), QString("PQ"));
        QVERIFY(win.codingTab()->isPqVehicleRowVisible());
        QVERIFY(!win.codingTab()->isMqbVehicleRowVisible());

        // Apply Raw Hex with 2-speaker PQ coding (B2=0x02) must NOT be overwritten by stale 4-speaker dropdown
        auto *hexInput = win.codingTab()->findChild<QLineEdit *>("hexInput");
        auto *spkBox = win.codingTab()->findChild<QComboBox *>("speakersCombo");
        auto *applyRawBtn = win.codingTab()->findChild<QPushButton *>("applyRawHexBtn");
        QVERIFY(hexInput != nullptr);
        QVERIFY(spkBox != nullptr);
        QVERIFY(applyRawBtn != nullptr);
        QCOMPARE(spkBox->currentData().toInt(), 4);
        hexInput->setText("0E000240020013");
        applyRawBtn->click();
        QCOMPARE(hexInput->text().trimmed().toUpper(), QString("0E000240020013"));
        QCOMPARE(spkBox->currentData().toInt(), 2);
        QVERIFY2(win.codingTab()->statusMessage().contains("VIP offline"),
                 qPrintable(win.codingTab()->statusMessage()));

        auto *platBox = win.codingTab()->findChild<QComboBox *>("platformCombo");
        QVERIFY(platBox != nullptr);
        const int mqbIdx = platBox->findData("MQB");
        QVERIFY(mqbIdx >= 0);
        platBox->setCurrentIndex(mqbIdx);
        QCOMPARE(win.codingTab()->currentPlatform(), QString("MQB"));
        QVERIFY(!win.codingTab()->isPqVehicleRowVisible());
        QVERIFY(win.codingTab()->isMqbVehicleRowVisible());
    }

    void testMainWindowDetectsExitedQemuZombie() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Fork a child that exits immediately and remains an unreaped zombie (simulating bash background QEMU)
        const pid_t child = ::fork();
        QVERIFY(child >= 0);
        if (child == 0) {
            ::_exit(0);
        }
        // Wait briefly so child enters Z state
        QTest::qWait(80);

        GuiConfig cfg;
        cfg.fwName = "5624_VW";
        cfg.codingFile = dir.filePath("z_coding.json");
        cfg.codingScript = QDir(QStringLiteral(RCD330_REPO_ROOT)).filePath("tools/rcd330_coding.py");
        cfg.autoConnect = false;
        cfg.qemuPid = static_cast<qint64>(child);

        MainWindow win(cfg);
        auto *badge = win.findChild<QLabel *>("statusBadge");
        QVERIFY(badge != nullptr);
        // Must detect zombie PID and mark QEMU exited within 2s
        QTRY_COMPARE_WITH_TIMEOUT(badge->text(), QString("QEMU process exited"), 2500);

        int status = 0;
        ::waitpid(child, &status, 0);
    }

    void testVehicleTabCl58LightSensorAndBatteryControls() {
        VehicleTab tab;
        QStringList cmds;
        connect(&tab, &VehicleTab::commandRequested, &tab, [&](const QString &c) { cmds << c; });

        // Bogus Fuel Level slider removed; CL_58, Light Sensor, and Battery controls present
        QVERIFY(tab.findChild<QSlider *>("fuelSlider") == nullptr);
        auto *cl58Off = tab.findChild<QPushButton *>("cl58OffBtn");
        auto *cl58On = tab.findChild<QPushButton *>("cl58OnBtn");
        auto *cl58Sl = tab.findChild<QSlider *>("cl58Slider");
        auto *lightSl = tab.findChild<QSlider *>("lightSlider");
        auto *battSl = tab.findChild<QSlider *>("battSlider");
        auto *battLowBtn = tab.findChild<QPushButton *>("battLowBtn");
        auto *battCritBtn = tab.findChild<QPushButton *>("battCritBtn");
        auto *battOkBtn = tab.findChild<QPushButton *>("battOkBtn");
        QVERIFY(cl58Off != nullptr);
        QVERIFY(cl58On != nullptr);
        QVERIFY(cl58Sl != nullptr);
        QVERIFY(lightSl != nullptr);
        QVERIFY(battSl != nullptr);
        QVERIFY(battLowBtn != nullptr);
        QVERIFY(battCritBtn != nullptr);
        QVERIFY(battOkBtn != nullptr);

        QCOMPARE(cl58Sl->value(), 0);
        QCOMPARE(lightSl->value(), 128);
        QCOMPARE(battSl->value(), 126);

        cl58On->click();
        QCOMPARE(cl58Sl->value(), 100);
        QVERIFY(cl58On->isChecked());
        QCOMPARE(cmds.last(), QString("CL58 100"));

        cl58Sl->setValue(60);
        QCOMPARE(cmds.last(), QString("CL58 60"));

        cl58Off->click();
        QCOMPARE(cl58Sl->value(), 0);
        QVERIFY(cl58Off->isChecked());
        QCOMPARE(cmds.last(), QString("CL58 0"));

        lightSl->setValue(220);
        QCOMPARE(cmds.last(), QString("LIGHT 220"));

        battLowBtn->click();
        QCOMPARE(battSl->value(), 112);
        QCOMPARE(cmds.last(), QString("BATT 112"));

        battCritBtn->click();
        QCOMPARE(battSl->value(), 102);
        QCOMPARE(cmds.last(), QString("BATT 102"));

        battOkBtn->click();
        QCOMPARE(battSl->value(), 126);
        QCOMPARE(cmds.last(), QString("BATT 126"));

        // resetForReboot restores defaults silently without emitting commands
        cl58Sl->setValue(80);
        lightSl->setValue(30);
        battSl->setValue(105);
        cmds.clear();
        tab.resetForReboot();
        QCOMPARE(cl58Sl->value(), 0);
        QVERIFY(cl58Off->isChecked());
        QCOMPARE(lightSl->value(), 128);
        QCOMPARE(battSl->value(), 126);
        QVERIFY(cmds.isEmpty());
    }
};

QTEST_MAIN(TestRcd330Gui)
#include "test_rcd330_gui.moc"
