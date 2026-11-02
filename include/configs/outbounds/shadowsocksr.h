#pragma once

#include "include/configs/common/Outbound.h"

namespace Configs {
    inline const QStringList shadowsocksrMethods = {"aes-256-cfb", "aes-192-cfb", "aes-128-cfb", "aes-256-ctr", "aes-192-ctr", "aes-128-ctr", "rc4-md5", "chacha20", "chacha20-ietf", "xchacha20", "none"};
    inline const QStringList shadowsocksrProtocols = {"origin", "auth_sha1_v4", "auth_aes128_md5", "auth_aes128_sha1", "auth_chain_a", "auth_chain_b"};
    inline const QStringList shadowsocksrObfs = {"plain", "http_simple", "http_post", "random_head", "tls1.2_ticket_auth", "tls1.2_ticket_fastauth"};

    class shadowsocksr : public outbound {
    public:
        QString method = "aes-256-cfb";
        QString password;
        QString protocol = "origin";
        QString protocol_param;
        QString obfs = "plain";
        QString obfs_param;

        bool ParseFromLink(const QString &link) override;
        bool ParseFromJson(const QJsonObject &object) override;
        bool ParseFromClash(const clash::Proxies &object) override;
        QString ExportToLink() override;
        QJsonObject ExportToJson() override;
        BuildResult Build() override;
        QString DisplayType() override { return "ShadowsocksR"; }
        SecurityInfo GetSecurity() override;
        void Validate(Validation::Issues &issues) override;
    };
}
