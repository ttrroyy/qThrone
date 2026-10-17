#pragma once
#include "include/configs/outbounds/extracore.h"
#include <QStringList>

namespace Configs {
// An S3 Bypass profile: the fedarisha outbound of SpaceNeuroX/s3-bypass, which carries traffic
// as objects in an S3 bucket. Imported from the original app's Xray configuration or our JSON.
class s3bypass : public extracore {
public:
    s3bypass() { server_port = 443; socksPort = 0; }
    static const QStringList &StorageKeys();
    static const QStringList &TuningKeys();
    // The fedarisha outbound of an Xray configuration, as the original app imports it.
    static bool IsXrayConfig(const QJsonObject &config);
    bool ParseFromXrayConfig(const QJsonObject &config);
    QJsonObject storage, tuning;
    bool ParseFromLink(const QString &) override { return false; }
    bool ParseFromJson(const QJsonObject &) override;
    QJsonObject ExportToJson() override;
    QJsonObject ExportIdentity() override;
    QString ExportToLink() override { return {}; }
    QString DisplayType() override { return "S3 Bypass"; }
    QString DisplayAddress() override;
    SecurityInfo GetSecurity() override { return {"S3", "HTTPS", SecurityLevel::Secure}; }
    QString Validate() const;
    QString Prepare();
    BuildResult Build() override;
    bool SupportsCredentialStrip() const override { return true; }
    void StripCredentials() override { storage.remove("accessKey"); storage.remove("secretKey"); }
private:
    QString bridgeAuth;
};
}
