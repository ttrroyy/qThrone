#pragma once
#include "include/configs/outbounds/extracore.h"
#include <QStringList>
#include <QUrlQuery>
#include <QUuid>

namespace Configs {
    // Dedicated profile UI; execution uses Throne's existing extra-core lifecycle.
    class qwdtt : public extracore {
    public:
        static int MaxWorkers(int hashCount) { return qBound(1, hashCount, 4) * 27; }
        static int NormalizeWorkers(int count, int hashCount) { return qBound(9, count, MaxWorkers(hashCount)) / 9 * 9; }
        qwdtt();
        [[nodiscard]] QString NativePingConfig(const QString &transportConfig) const;
        QString hashSource = "manual";
        int autoHashCount = 1;
        QString mode = "wg";
        QString password;
        QStringList hashes;
        int rawPort = 56003;
        int workers = 9;
        bool turnTCP = true;
        QString obfs = "audio";
        QString vkAnonPath = "vkcalls";
        QString captchaMode = "auto";
        QString goDNS = "yandex";
        QString deviceID;
        // Imported from a plain wdtt://host:dtls:wg:local:pass link: shown as WDTT and shared in that form.
        bool legacy = false;
        int wgPort = 56001;
        int localPort = 9000;

        bool ParseFromLink(const QString &link) override;
        bool ParseFromJson(const QJsonObject &object) override;
        QJsonObject ExportToJson() override;
        QJsonObject ExportIdentity() override;
        QString ExportToLink() override;
        QString DisplayType() override { return QString(legacy ? "WDTT" : "qWDTT") + (mode == "wg" ? " (WG)" : " (RAW)"); }
        SecurityInfo GetSecurity() override { return {"RTP AEAD", turnTCP ? "TURN TCP" : "TURN UDP", SecurityLevel::Secure}; }
        QString Prepare();
        BuildResult Build() override;
        bool SupportsCredentialStrip() const override { return true; }
        void StripCredentials() override { password.clear(); hashes.clear(); deviceID.clear(); }
        static QStringList NormalizeHashes(const QStringList &input);
        // A call hash from what the user pastes: the hash itself or a VK call link, as stripVkUrlStatic
        // of the original qWDTT and CSQTT apps reads it.
        static QString StripVkCallLink(const QString &input);
        // The server's profile API, as the original app's profile list uses it: the device slots of a
        // password, each request signed with the password over a one-time server nonce.
        struct DeviceSlots { bool ok = false; int max = 1, bound = 0, active = 0; bool current = false; };
        static DeviceSlots FetchDeviceSlots(const QString &server, int port, const QString &password, const QString &device);
        static bool UnbindDevice(const QString &server, int port, const QString &password, const QString &device);
        static QUrlQuery ProfileProof(const QString &action, const QString &password, const QString &device, const QString &nonce);
    private:
        QString bridgeAuth;
    };
}
