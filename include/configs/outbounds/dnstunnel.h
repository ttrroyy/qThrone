#pragma once
#include "include/configs/outbounds/extracore.h"
#include <QJsonArray>
#include <QStringList>

namespace Configs {
// A DNS tunnel profile: DNSTT, VayDNS, SlipStream or MasterDNS. The fields are those of
// the dnst:// format (net2share/dnst-url-spec); links of the apps that predate it are read
// too (slipnet://, masterdns://). Without resolvers of its own a profile uses the network's
// DNS servers, which the helper reads from the system.
class dnstunnel : public extracore {
public:
    explicit dnstunnel(const QString &kind = "dnstt") : kind(kind) { server_port = 0; socksPort = 0; }
    static const QStringList &Kinds();
    static const QStringList &RecordTypes();
    static QString Title(const QString &kind);
    // Which of the four transports this profile is; the profile's type follows it.
    QString kind;
    // The dnst:// fields (domain, pubkey, resolvers, backend, ...), without type and name.
    QJsonObject data;
    bool ParseFromLink(const QString &) override;
    bool ParseFromJson(const QJsonObject &) override;
    QJsonObject ExportToJson() override;
    QJsonObject ExportIdentity() override;
    QString ExportToLink() override;
    QString DisplayType() override { return Title(kind); }
    QString DisplayAddress() override { return data.value("domain").toString(); }
    SecurityInfo GetSecurity() override;
    QString Validate() const;
    QString Prepare();
    BuildResult Build() override;
    bool SupportsCredentialStrip() const override { return true; }
    void StripCredentials() override;
private:
    QString bridgeAuth;
    bool parseDnst(const QString &body);
    bool parseSlipNet(const QString &body);
    bool parseWhiteDns(const QString &body);
};
}
