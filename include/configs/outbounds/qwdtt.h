#pragma once
#include "include/configs/outbounds/extracore.h"
#include <QStringList>
#include <QUuid>

namespace Configs {
    // Dedicated profile UI; execution uses Throne's existing extra-core lifecycle.
    class qwdtt : public extracore {
    public:
        qwdtt() { server_port = 56000; socksPort = 0; deviceID = QUuid::createUuid().toString(QUuid::WithoutBraces); }
        QString mode = "raw";
        QString password;
        QStringList hashes;
        int rawPort = 56003;
        int workers = 9;
        bool turnTCP = false;
        QString obfs = "audio";
        QString goDNS = "yandex";
        QString deviceID;

        bool ParseFromLink(const QString &link) override;
        bool ParseFromJson(const QJsonObject &object) override;
        QJsonObject ExportToJson() override;
        QJsonObject ExportIdentity() override;
        QString ExportToLink() override;
        QString DisplayType() override { return mode == "wg" ? "qWDTT (WG)" : "qWDTT (RAW)"; }
        SecurityInfo GetSecurity() override { return {"RTP AEAD", turnTCP ? "TURN TCP" : "TURN UDP", SecurityLevel::Secure}; }
        QString Prepare();
        BuildResult Build() override;
        bool SupportsCredentialStrip() const override { return true; }
        void StripCredentials() override { password.clear(); hashes.clear(); deviceID.clear(); }
        static QStringList NormalizeHashes(const QStringList &input);
    private:
        QString bridgeAuth;
    };
}
