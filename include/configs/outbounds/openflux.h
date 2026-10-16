#pragma once
#include "include/configs/outbounds/extracore.h"
#include <QJsonArray>
#include <QStringList>

namespace Configs {
// An openflux:// profile, read and checked the way OpenFlux's share package does it.
// The helper hands it to OpenFlux's own client code; nothing (the encryption context
// included) is derived here.
class openflux : public extracore {
public:
    openflux() { server_port = 18445; socksPort = 0; }
    // share.knownTypes; MAX (oneme) belongs to one account and is never shared.
    static const QStringList &CarrierTypes();
    QString secret, context, codec, mode;
    bool negotiate = false;
    QJsonArray transports;
    // Keys of a link this client has no field for; they travel on unchanged.
    QJsonObject extra;
    bool ParseFromLink(const QString &) override;
    bool ParseFromJson(const QJsonObject &) override;
    QJsonObject ExportToJson() override;
    QJsonObject ExportIdentity() override;
    QString ExportToLink() override;
    QString DisplayType() override { return "OpenFlux"; }
    SecurityInfo GetSecurity() override { return {"OpenFlux AEAD", "Documents / Direct", SecurityLevel::Secure}; }
    QString Validate() const;
    QString Prepare();
    BuildResult Build() override;
    bool SupportsCredentialStrip() const override { return true; }
    void StripCredentials() override { secret.clear(); transports = {}; }
private:
    QString bridgeAuth;
};
}
