#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

struct CodingPayload {
    QString platform = "PQ";
    QString brand = "VW";
    QString canSpeed = "HS";
    QString partNumber = "6RD035187B ";
    QString opsMode = "8K";
    int speakers = 4;
    bool engMode = false;
    int vehicleMqb = 0x38;
    QString codingHex;

    bool btEnabled = true;
    bool rvcEnabled = true;
    bool extMicEnabled = false;
    bool extAmpEnabled = false;
    bool mirrorlinkEnabled = true;
    bool carplayEnabled = true;
    bool androidAutoEnabled = true;
    bool carlifeEnabled = false;
    int vehiclePq = 19;
    bool amDisabled = false;
    int bootlogoId = 0x18;
    bool drivingLock = false;
    QStringList validationWarnings;

    QStringList vipCmds;
    QString codingFile;
    QMap<int, QString> pqVehicles;
    QMap<int, QString> mqbVehicles;
    QMap<int, QString> mqbBootlogos;
};

class CodingBridge {
public:
    CodingBridge(QString scriptPath, QString codingFile, QString fwId);

    bool load(CodingPayload &out, QString &errorOut) const;
    bool apply(const QJsonObject &update, bool live, CodingPayload &out, QString &errorOut) const;
    bool reset(bool live, CodingPayload &out, QString &errorOut) const;

private:
    bool runSubcommand(const QStringList &args, const QByteArray &stdinData,
                       CodingPayload &out, QString &errorOut) const;
    static bool parsePayload(const QByteArray &raw, CodingPayload &out, QString &errorOut);

    QString m_scriptPath;
    QString m_codingFile;
    QString m_fwId;
};
