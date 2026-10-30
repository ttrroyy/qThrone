#pragma once
#include "include/configs/outbounds/extracore.h"
#include <QJsonObject>
#include <QStringList>
#include <memory>

namespace Configs {
class qwdtt;
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
    // A plain WDTT link names no client: "wdttplus" (original WDTT protocol) or "qwdtt" for servers that speak its WRAP.
    QString engine = "qwdtt";
    // qWDTT engine only.
    QString mode = "wg";
    int rawPort = 56003;
    QString captchaMode = "auto";
    bool tcpOnly = false;
    bool rtMasque = false;
    bool rtMasqueServerBootstrap = false;
    bool vkCallsPreflight = true;
    QString turnSni = "ya.ru";
    QString fingerprint = "firefox";
    QString clientIds = "6287487,8202606";
    static bool ValidTurnSni(const QString &value);
    QString deviceID;
    // normalizeTunnelWorkerCount of WDTT Plus: groups of 9, up to 108 or the profile's own limit.
    static int NormalizeWorkers(int requested, int profileMaxWorkers);
    static int WorkerLimit(int profileMaxWorkers);
    // buildDeviceInfoJson of WDTT Plus, filled with this computer's details.
    static QString DeviceInfo();
    bool ParseFromLink(const QString &) override;
    bool ParseFromJson(const QJsonObject &) override;
    QJsonObject ExportToJson() override;
    QJsonObject ExportIdentity() override;
    QString ExportToLink() override;
    QString DisplayType() override {
        if (!legacy) return "WDTT Plus";
        return engine == "qwdtt" ? QString("WDTT · qWDTT (") + (mode == "raw" ? "RAW" : "WG") + ")" : "WDTT";
    }
    SecurityInfo GetSecurity() override { return {"DTLS", "TURN", SecurityLevel::Secure}; }
    QString Prepare();
    BuildResult Build() override;
    bool SupportsCredentialStrip() const override { return true; }
    void StripCredentials() override { password.clear(); hashes.clear(); deviceID.clear(); }
private:
    // Set by Prepare when the qWDTT engine runs this profile.
    std::shared_ptr<qwdtt> qwdttEngine;
    QString bridgeUser;
    QString bridgePass;
};
}
