#pragma once
#include "include/configs/outbounds/extracore.h"
#include <QUuid>
namespace Configs {
class olcrtc : public extracore {
public:
    olcrtc() { socksPort = 0; deviceID = QUuid::createUuid().toString(QUuid::WithoutBraces); }
    QString provider = "jitsi", transport = "datachannel", room, key, token, channel;
    QString dns = "8.8.8.8:53", deviceID;
    QJsonObject options;
    bool ParseFromLink(const QString &) override;
    bool ParseFromJson(const QJsonObject &) override;
    QJsonObject ExportToJson() override;
    QJsonObject ExportIdentity() override;
    QString ExportToLink() override;
    QString DisplayType() override { return "olcRTC"; }
    SecurityInfo GetSecurity() override { return {"XChaCha20-Poly1305", "WebRTC", SecurityLevel::Secure}; }
    QString Validate() const;
    QString Prepare();
    BuildResult Build() override {
        auto r = extracore::Build(); r.object["network"] = "tcp";
        // The helper's loopback listener accepts only this launch's credentials.
        if (!bridgeAuth.isEmpty()) { r.object["version"] = "5"; r.object["username"] = bridgeAuth; r.object["password"] = bridgeAuth; }
        return r;
    }
    bool SupportsCredentialStrip() const override { return true; }
    void StripCredentials() override { key.clear(); token.clear(); }
private:
    QString bridgeAuth;
};
}
