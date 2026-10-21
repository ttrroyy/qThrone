#pragma once
#include "include/configs/outbounds/extracore.h"
#include <QJsonObject>
#include <QStringList>

namespace Configs {
// WDTT Plus (github.com/Ivan4537/WDTT-Plus), run by its own client behind the qwdtt
// helper. A plain wdtt:// link of the original WDTT gives the same profile, shown as WDTT.
class wdttplus : public extracore {
public:
    wdttplus();
    QString password;
    // "auto" takes call hashes from the qWDTT VK binding; WDTT Plus never logs into VK itself.
    QString hashSource = "manual";
    int autoHashCount = 1;
    QStringList hashes;
    int workers = 18;
    // The server's limit from a wdtt://connect link, 0 when it named none.
    int maxWorkers = 0;
    int wgPort = 56001;
    int localPort = 9000;
    bool legacy = false;
    QString captchaMode = "auto";
    bool tcpOnly = false;
    bool rtMasque = false;
    bool rtMasqueServerBootstrap = false;
    bool vkCallsPreflight = true;
    QString turnSni;
    QString fingerprint = "firefox";
    QString clientIds = "6287487,8202606";
    static bool ValidTurnSni(const QString &value);
    QString deviceID;
    // normalizeTunnelWorkerCount of WDTT Plus: groups of 9, up to 108 or the profile's own limit.
    static int NormalizeWorkers(int requested, int profileMaxWorkers);
    static int WorkerLimit(int profileMaxWorkers);
    // buildDeviceInfoJson of WDTT Plus, filled with this computer's details.
    static QString DeviceInfo();
    // A plain wdtt:// profile that 1.5 and 1.5.1 stored as WDTT Plus, as the qWDTT profile it was before; empty otherwise.
    static QJsonObject LegacyToQwdtt(const QJsonObject &object);
    bool ParseFromLink(const QString &) override;
    bool ParseFromJson(const QJsonObject &) override;
    QJsonObject ExportToJson() override;
    QJsonObject ExportIdentity() override;
    QString ExportToLink() override;
    QString DisplayType() override { return legacy ? "WDTT" : "WDTT Plus"; }
    SecurityInfo GetSecurity() override { return {"DTLS", "TURN", SecurityLevel::Secure}; }
    QString Prepare();
    BuildResult Build() override;
    bool SupportsCredentialStrip() const override { return true; }
    void StripCredentials() override { password.clear(); hashes.clear(); deviceID.clear(); }
private:
    QString bridgeUser;
    QString bridgePass;
};
}
