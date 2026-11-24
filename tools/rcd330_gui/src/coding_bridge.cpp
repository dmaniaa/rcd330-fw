#include "coding_bridge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>

CodingBridge::CodingBridge(QString scriptPath, QString codingFile, QString fwId)
    : m_scriptPath(std::move(scriptPath)),
      m_codingFile(std::move(codingFile)),
      m_fwId(std::move(fwId)) {}

bool CodingBridge::load(CodingPayload &out, QString &errorOut) const {
    return runSubcommand({"load", "--coding-file", m_codingFile, "--fw-id", m_fwId}, {}, out, errorOut);
}

bool CodingBridge::apply(const QJsonObject &update, bool live, CodingPayload &out, QString &errorOut) const {
    QStringList args{"apply", "--coding-file", m_codingFile, "--fw-id", m_fwId};
    if (live) args << "--live";
    return runSubcommand(args, QJsonDocument(update).toJson(QJsonDocument::Compact), out, errorOut);
}

bool CodingBridge::reset(bool live, CodingPayload &out, QString &errorOut) const {
    QStringList args{"reset", "--coding-file", m_codingFile, "--fw-id", m_fwId};
    if (live) args << "--live";
    return runSubcommand(args, {}, out, errorOut);
}

bool CodingBridge::runSubcommand(const QStringList &args, const QByteArray &stdinData,
                                 CodingPayload &out, QString &errorOut) const {
    QProcess proc;
    QStringList fullArgs;
    fullArgs << m_scriptPath << args;
    proc.start("python3", fullArgs);
    if (!proc.waitForStarted(3000)) {
        errorOut = "Failed to launch python3 for rcd330_coding.py";
        return false;
    }
    if (!stdinData.isEmpty()) {
        proc.write(stdinData);
    }
    proc.closeWriteChannel();
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        errorOut = "rcd330_coding.py timed out";
        return false;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        errorOut = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (errorOut.isEmpty()) errorOut = QString("rcd330_coding.py exited with code %1").arg(proc.exitCode());
        return false;
    }
    return parsePayload(proc.readAllStandardOutput(), out, errorOut);
}

bool CodingBridge::parsePayload(const QByteArray &raw, CodingPayload &out, QString &errorOut) {
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        errorOut = QString("Invalid JSON from rcd330_coding.py: %1").arg(perr.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    const QJsonObject cfg = root["config"].toObject();
    const QJsonObject dec = root["decoded"].toObject();

    out = CodingPayload{};
    out.platform = cfg["platform"].toString("PQ");
    out.brand = cfg["brand"].toString("VW");
    out.canSpeed = cfg["can_speed"].toString("HS");
    out.partNumber = cfg["part_number"].toString("6RD035187B ");
    out.opsMode = cfg["ops_mode"].toString("8K");
    out.speakers = cfg["speakers"].toInt(4);
    out.engMode = cfg["eng_mode"].toBool(false);
    out.vehicleMqb = cfg["vehicle_mqb"].toInt(0x38);
    out.codingHex = cfg["coding_hex"].toString();

    out.btEnabled = dec["bt_enabled"].toBool(true);
    out.rvcEnabled = dec["rvc_enabled"].toBool(true);
    out.extMicEnabled = dec["ext_mic_enabled"].toBool(false);
    out.extAmpEnabled = dec["ext_amp_enabled"].toBool(false);
    out.mirrorlinkEnabled = dec["mirrorlink_enabled"].toBool(true);
    out.carplayEnabled = dec["carplay_enabled"].toBool(true);
    out.androidAutoEnabled = dec["android_auto_enabled"].toBool(true);
    out.carlifeEnabled = dec["carlife_enabled"].toBool(false);
    out.vehiclePq = dec["vehicle_pq"].toInt(19);
    out.amDisabled = dec["am_disabled"].toBool(false);
    out.bootlogoId = dec["bootlogo_id"].toInt(0x18);
    out.drivingLock = dec["driving_lock"].toBool(false);

    for (const QJsonValue &w : dec["validation_warnings"].toArray()) {
        out.validationWarnings.append(w.toString());
    }
    for (const QJsonValue &c : root["vip_cmds"].toArray()) {
        out.vipCmds.append(c.toString());
    }
    out.codingFile = root["coding_file"].toString();

    auto parseMap = [](const QJsonObject &obj, QMap<int, QString> &dst) {
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            bool ok = false;
            const int k = it.key().toInt(&ok);
            if (ok) dst.insert(k, it.value().toString());
        }
    };
    parseMap(root["pq_vehicles"].toObject(), out.pqVehicles);
    parseMap(root["mqb_vehicles"].toObject(), out.mqbVehicles);
    parseMap(root["mqb_bootlogos"].toObject(), out.mqbBootlogos);
    return true;
}
