#pragma once
#include "include/configs/outbounds/extracore.h"
namespace Configs {
class sstp : public extracore {
public:
    sstp() { server_port = 443; socksPort = 0; }
    QString username, password, sni;
    bool allowInsecure = false;
    bool useServerDNS = false;
    bool ParseFromJson(const QJsonObject &) override;
    bool ParseFromLink(const QString &) override;
    QJsonObject ExportToJson() override;
    QJsonObject ExportIdentity() override;
    QString ExportToLink() override;
    QString DisplayType() override { return "SSTP"; }
    SecurityInfo GetSecurity() override { return {"SSTP", "TLS", allowInsecure ? SecurityLevel::Weak : SecurityLevel::Secure}; }
    QString Validate() const;
    QString Prepare();
    BuildResult Build() override;
    bool SupportsCredentialStrip() const override { return true; }
    void StripCredentials() override { username.clear(); password.clear(); }
private:
    QString bridgeAuth;
};
}
