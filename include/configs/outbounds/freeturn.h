#pragma once
#include "include/configs/outbounds/extracore.h"
#include <QJsonObject>
namespace Configs {
class freeturn : public extracore {
public:
    freeturn() { server_port = 56000; socksPort = 0; }
    QString provider = "vk", links, relayTransport = "tcp", tunnelMode = "udp", obfProfile = "none", obfKey, clientId, dnsMode = "auto", dnsServers, turnHost, wgConfig;
    int threads = 12, streamsPerCred = 12, obfTiming = 0;
    bool bond = false, manualCaptcha = false;
    QJsonObject kcp;
    bool ParseFromJson(const QJsonObject &) override;
    bool ParseFromLink(const QString &) override;
    QJsonObject ExportToJson() override;
    QJsonObject ExportIdentity() override;
    QString ExportToLink() override;
    QString DisplayType() override { return provider == "direct" ? QStringLiteral("FreeTurn (Direct)") : QStringLiteral("FreeTurn (VK)"); }
    /** What makes a profile storable; a call link or WireGuard configuration may still come later. */
    QString Validate() const;
    QString Prepare();
    BuildResult Build() override;
    bool SupportsCredentialStrip() const override { return true; }
    void StripCredentials() override { obfKey.clear(); wgConfig.clear(); clientId.clear(); }
private:
    QStringList linkList() const;
    QString bridgeAuth;
};
}
