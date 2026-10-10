#pragma once
#include "include/configs/outbounds/extracore.h"
#include <QStringList>
#include <QUuid>

namespace Configs {
class csqtt : public extracore {
public:
    csqtt() { server_port = 46000; socksPort = 0; deviceID = QUuid::createUuid().toString(QUuid::WithoutBraces); }
    QString password;
    QStringList hashes;
    int workers = 18;
    bool turnTCP = false;
    QString obfs = "audio";
    QString vkAnonPath = "vkcalls";
    QString captchaMode = "auto";
    QString deviceID;
    static int MaxWorkers(int count) { return qMin(126, qBound(1, count, 6)*27); }
    static int NormalizeWorkers(int requested, int count) { return qBound(9, requested, MaxWorkers(count))/9*9; }
    static QStringList NormalizeHashes(const QStringList &input);
    bool ParseFromLink(const QString &) override;
    bool ParseFromJson(const QJsonObject &) override;
    QJsonObject ExportToJson() override;
    QJsonObject ExportIdentity() override;
    QString ExportToLink() override;
    QString DisplayType() override { return turnTCP ? "CSQTT (TCP)" : "CSQTT (UDP)"; }
    SecurityInfo GetSecurity() override { return {"CSQTT WRAP", turnTCP ? "TURN TCP/TLS" : "TURN UDP", SecurityLevel::Secure}; }
    QString Prepare();
    BuildResult Build() override;
    bool SupportsCredentialStrip() const override { return true; }
    void StripCredentials() override { password.clear(); hashes.clear(); deviceID.clear(); }
private:
    QString bridgeAuth;
};
}
