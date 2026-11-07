#pragma once
#include "include/configs/outbounds/extracore.h"
#include <QJsonObject>
#include <QStringList>

namespace Configs {
// The MRTT transport: its own client over VK, MAX or Jitsi. The numbers are those of the original app's WorkerCountPolicy.
class mrtt : public extracore {
public:
    static constexpr int WorkersPerGroup = 9, GroupsPerHash = 3, MaxHashes = 6, MaxWorkers = 126, MinJitsiWorkers = 3, MaxJitsiWorkers = 16;
    mrtt();
    QString password;
    // vk, max or jitsi. A link carries vk or jitsi; MAX is private data of this client.
    QString transportMode = "vk";
    // The shared VK binding of the client, as in the other VK transports; auto_js uses its token unless the link brings one.
    QString hashSource = "manual";
    QString vkAccessToken;
    int autoHashCount = 1;
    QStringList hashes;
    int workers = 18;
    // A link's group; empty takes the bound one.
    QString maxGroup;
    QString jitsiRoom;
    int jitsiSeats = 0;
    int jitsiWorkers = 6;
    QString turnTransport = "udp";
    QString srtpMode = "auto";
    QString fingerprint = "firefox";
    QString clientIds = "8202606,6287487";
    QString customTurnIps;
    QString vkAnonPath = "vkcalls";
    QString captchaMode = "auto";
    int mtu = 1280;
    static QStringList NormalizeHashes(const QStringList &input);
    static int MaxWorkersFor(int hashCount, bool anyCount);
    static int NormalizeWorkers(int requested, int hashCount, bool anyCount);
    static bool ValidJitsiRoom(const QString &room);
    /** A conference link (https://meet.example.org/Room) as host/room; empty when it is not one. */
    static QString ParseJitsiRoom(const QString &input);
    static bool ValidMaxGroup(const QString &group);
    static bool ValidPassword(const QString &password);
    /** The original link's encryption, through the bundled helper: mrtt:// or mrttsub:// for plain text, plain text for a link. */
    static QString SharePayload(const QString &value, bool encrypt);
    bool ParseFromLink(const QString &) override;
    bool ParseFromJson(const QJsonObject &) override;
    QJsonObject ExportToJson() override;
    QJsonObject ExportIdentity() override;
    QString ExportToLink() override;
    QString DisplayType() override;
    SecurityInfo GetSecurity() override { return {"MRTT " + srtpMode.toUpper(), transportMode.toUpper(), SecurityLevel::Secure}; }
    QString Prepare();
    BuildResult Build() override;
    bool SupportsCredentialStrip() const override { return true; }
    void StripCredentials() override { password.clear(); hashes.clear(); vkAccessToken.clear(); }
private:
    QString bridgeAuth;
};
}
