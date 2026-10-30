#pragma once
#include "include/configs/outbounds/extracore.h"
#include <QStringList>
#include <QUuid>

namespace Configs {
class csqtt : public extracore {
public:
    csqtt();
    QString password;
    // Shared qWDTT account binding; auto_js uses a launch-only token unless the link provides one.
    QString hashSource = "manual";
    QString vkAccessToken;
    int autoHashCount = 1;
    QStringList hashes;
    int workers = 10;
    bool turnTCP = false;
    QString obfs = "unified";
    QString turnTransport = "udp";
    QString srtpMode = "auto";
    QString fingerprint = "firefox";
    QString clientIds = "8202606,6287487";
    QString customTurnIps;
    QString vkAnonPath = "vkcalls";
    QString captchaMode = "auto";
    // The client the server speaks: "2.5.0" (2.5.0 and newer) or "2.1.9", the original device-bound protocol.
    QString version = "2.5.0";
    QString deviceID;
    static int MaxWorkers(int count) { return 100; }
    static int NormalizeWorkers(int requested, int count) { return requested <= 5 ? 5 : qMax(5, qMin(requested, 100)/10*10); }
    static QStringList NormalizeHashes(const QStringList &input);
    bool ParseFromLink(const QString &) override;
    bool ParseFromJson(const QJsonObject &) override;
    QJsonObject ExportToJson() override;
    QJsonObject ExportIdentity() override;
    QString ExportToLink() override;
    QString DisplayType() override { return (version == "2.1.9" ? "CSQTT 2.1.9 (" : "CSQTT (") + turnTransport.toUpper() + ")"; }
    SecurityInfo GetSecurity() override { return {"CSQTT " + srtpMode.toUpper(), "TURN " + turnTransport.toUpper(), SecurityLevel::Secure}; }
    QString Prepare();
    BuildResult Build() override;
    bool SupportsCredentialStrip() const override { return true; }
    void StripCredentials() override { password.clear(); hashes.clear(); deviceID.clear(); vkAccessToken.clear(); }
private:
    QString bridgeAuth;
};
}
