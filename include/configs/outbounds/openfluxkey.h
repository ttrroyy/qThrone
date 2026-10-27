#pragma once
#include "include/configs/outbounds/extracore.h"
#include <QStringList>

namespace Configs {
// A profile of the wlruscfd/openflux-server fork: the openflux://import?data=<base64url JSON> link its control
// plane issues (controlplane/internal/api/deeplink.go), read as openflux-app's ProfileDeepLink reads it. The fork's
// exit nodes frame traffic their own way, so it runs through the fork's own client (script/openfluxkey).
class openfluxkey : public extracore {
public:
    openfluxkey() { server_port = 443; socksPort = 0; }
    static const QStringList &Transports();
    QJsonObject data;
    bool ParseFromLink(const QString &link) override;
    bool ParseFromJson(const QJsonObject &object) override;
    QJsonObject ExportToJson() override;
    QJsonObject ExportIdentity() override;
    QString ExportToLink() override;
    QString DisplayType() override { return "OpenFlux Key"; }
    QString DisplayAddress() override;
    SecurityInfo GetSecurity() override { return {"OpenFlux", "Documents", SecurityLevel::Secure}; }
    QString Validate() const;
    QString Prepare();
    BuildResult Build() override;
    bool SupportsCredentialStrip() const override { return true; }
    void StripCredentials() override { data.remove("key_token"); data.remove("max_token"); }
private:
    QStringList documents() const;
    QString bridgeAuth;
};
}
