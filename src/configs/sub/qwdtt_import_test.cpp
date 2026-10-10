#include "include/configs/sub/SubscriptionParser.hpp"
#include "include/configs/outbounds/qwdtt.h"
#include "include/configs/outbounds/csqtt.h"
#include "include/configs/generate.h"
#include "include/global/Configs.hpp"
#include "include/database/ProfilesRepo.h"
#include "include/database/GroupsRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/database/OtpProfilesRepo.h"
#include "include/database/TrafficStatsRepo.h"
#include "include/database/MarkersRepo.h"
#include "include/database/IpListsRepo.h"
#include "include/database/IpScansRepo.h"
#include <QDebug>
#include <QTemporaryDir>
#include <QScopeGuard>
#include "include/global/Version.hpp"
#include "include/ui/profile/edit_qwdtt.h"
#include "include/ui/profile/edit_csqtt.h"

int TestQwdttImport() {
    qInstallMessageHandler(nullptr);
    // This entry point runs before normal application/database initialization.
    // Configuration generation needs repositories, all owned by this fixture.
    QTemporaryDir fixture;
    if (!fixture.isValid()) return 1;
    auto *previousManager = Configs::dataManager;
    Configs::initDB(fixture.filePath("throne.db").toStdString());
    const auto restoreManager = qScopeGuard([previousManager] {
        delete Configs::dataManager;
        Configs::dataManager = previousManager;
    });
    int failures = 0;
    const auto check = [&failures](bool ok, const char *name) {
        if (!ok) { qCritical() << "qWDTT import test failed:" << name; ++failures; }
    };
    const QByteArray android = "qwdtt://config?name=Main+%28demo%29&peer=203.0.113.10%3A56000&hashes=hash1%2Chash2&workers=9&port=9000&pass=a%2Bb+%26%25";
    const auto parse = [](const QByteArray &body) {
        QList<std::shared_ptr<Configs::Profile>> profiles;
        Subscription::ParseSink sink;
        sink.profile = [&profiles](auto profile) { profiles.append(profile); };
        Subscription::ParseDocument(body, sink);
        return profiles;
    };
    const QByteArray csqttLink = "csqtt://connect?v=2&host=203.0.113.7&peer=46000&password=p%40ss&hashes=abcdefghijklmno%2Bp+abcdefghijklmnop2";
    const auto csProfiles = parse(csqttLink);
    check(csProfiles.size() == 1, "CSQTT subscription link imports one profile");
    if (csProfiles.size() == 1) {
        auto *cs = dynamic_cast<Configs::csqtt *>(csProfiles.first()->outbound.get());
        check(cs && cs->hashes.size() == 2 && cs->hashes.first() == "abcdefghijklmno+p", "CSQTT hash plus survives URL decoding");
        if (cs) {
            check(cs->DisplayType() == "CSQTT (UDP)", "CSQTT default transport label");
            cs->turnTCP = true;
            check(cs->DisplayType() == "CSQTT (TCP)", "CSQTT TCP transport label");
            Configs::csqtt roundtrip;
            cs->name = QString::fromUtf8("Тест + # CSQTT");
            check(roundtrip.ParseFromLink(cs->ExportToLink()) && roundtrip.hashes == cs->hashes && roundtrip.password == cs->password, "CSQTT original link round trip");
            check(roundtrip.name == cs->name, "CSQTT profile name survives link round trip");
            Configs::csqtt named;
            check(named.ParseFromLink(QString::fromUtf8(csqttLink) + "&remark=Panel%20name") && named.name == "Panel name", "CSQTT panel remark imports as profile name");
        }
        EditCSQTT csEditor;
        csEditor.onStart(csProfiles.first());
        auto *csWorkers = csEditor.findChild<QComboBox *>("csqttWorkers");
        for (int count = 1; count <= 6; ++count) {
            auto *hash = csEditor.findChild<QLineEdit *>(QString("csqttHash%1").arg(count));
            check(hash != nullptr, "CSQTT editor exposes six hash fields");
            if (hash) hash->setText(QString("abcdefghijklmnop%1").arg(count));
        }
        check(csWorkers && csWorkers->count() == 14 && csWorkers->itemData(13).toInt() == 126, "CSQTT selector caps six hashes at 126 workers");
    }
    check(parse("csqtt://connect?v=3&host=203.0.113.7&peer=46000&password=secret").isEmpty(), "unknown CSQTT link version rejected");
    const auto excessCSQTT = parse("csqtt://connect?v=2&host=203.0.113.7&peer=46000&password=secret&hashes=abcdefghijklmnop1+abcdefghijklmnop2+abcdefghijklmnop3+abcdefghijklmnop4+abcdefghijklmnop5+abcdefghijklmnop6+ignored");
    check(excessCSQTT.size() == 1, "excess CSQTT hashes do not prevent import");
    if (excessCSQTT.size() == 1) {
        auto *cs = dynamic_cast<Configs::csqtt *>(excessCSQTT.first()->outbound.get());
        check(cs && cs->hashes.size() == 6 && cs->hashes.first() == "abcdefghijklmnop1" && cs->hashes.last() == "abcdefghijklmnop6", "CSQTT retains first six hashes in order");
    }
    Configs::qwdtt defaults;
    check(defaults.mode == "raw" && defaults.turnTCP, "new profiles default to RAW TCP");
    check(defaults.ParseFromLink(QString::fromUtf8(android)) && defaults.turnTCP, "Android links default to TCP");
    check(defaults.ParseFromLink(QString::fromUtf8(android) + "&turn_tcp=0") && !defaults.turnTCP, "explicit UDP is preserved");
    check(defaults.ParseFromLink(QString::fromUtf8(android) + "&vk_anon_path=legacy&captcha_mode=wv"), "captcha mode import");
    Configs::qwdtt exported;
    check(exported.ParseFromLink(defaults.ExportToLink()) && exported.vkAnonPath == "legacy" && exported.captchaMode == "wv", "captcha settings round trip");
    auto testProfiles = parse(android);
    const auto fixtureGroups = Configs::dataManager->groupsRepo->GetAllGroupIds();
    check(!fixtureGroups.isEmpty(), "isolated repository has a group");
    if (!fixtureGroups.isEmpty()) {
        for (auto &profile : testProfiles) check(Configs::dataManager->profilesRepo->AddProfile(profile, fixtureGroups.first()), "save test profile to isolated repository");
    }
    auto tests = Configs::BuildTestConfig(testProfiles);
    check(tests->error.isEmpty() && tests->fullConfigs.size() == 1 && tests->qwdttConfigs.size() == 1, "qWDTT test config uses a session-owned bridge");
    if (!testProfiles.isEmpty()) {
        const auto &settings = Configs::dataManager->settingsRepo;
        const bool oldTun = settings->spmode_vpn;
        const bool oldIPv6 = settings->vpn_ipv6;
        settings->spmode_vpn = true;
        settings->vpn_ipv6 = true;
        const auto live = Configs::BuildSingBoxConfig(testProfiles.first());
        settings->spmode_vpn = oldTun;
        settings->vpn_ipv6 = oldIPv6;
        check(live->error.isEmpty(), "qWDTT live configuration builds");
        for (const auto &inbound : live->coreConfig["inbounds"].toArray()) {
            const auto tun = inbound.toObject();
            if (tun["type"] == "tun") check(tun["address"].toArray().size() == 1,
                "qWDTT TUN remains IPv4-only without changing saved IPv6 preferences");
        }
        const auto dnsRules = live->coreConfig["dns"].toObject()["rules"].toArray();
        const auto guard = dnsRules.isEmpty() ? QJsonObject{} : dnsRules.first().toObject();
        check(guard["query_type"].toArray() == QJsonArray{"AAAA"} && guard["action"] == "predefined",
              "qWDTT IPv4 bridge suppresses unusable IPv6 DNS answers");
    }
    EditQWDTT editor;
    auto *workers = editor.findChild<QComboBox *>("qwdttWorkers");
    check(workers != nullptr, "worker dropdown exists");
    for (int count = 1; count <= 4; ++count) {
        auto *hash = editor.findChild<QLineEdit *>(QString("qwdttHash%1").arg(count));
        check(hash != nullptr, "hash field exists");
        if (hash) hash->setText(QString("hash%1").arg(count));
        check(workers && workers->count() == count * 3 && workers->itemData(workers->count() - 1).toInt() == count * 27, "worker dropdown depends on hashes");
    }
    if (workers) workers->setCurrentIndex(workers->findData(108));
    for (int count = 4; count > 1; --count) {
        if (auto *hash = editor.findChild<QLineEdit *>(QString("qwdttHash%1").arg(count))) hash->clear();
        check(workers && workers->currentData().toInt() == (count - 1) * 27, "clamp workers after removing a hash");
    }
    for (const auto &body : {android, android.toBase64(), QByteArray("`") + android + "`", QByteArray("QWDTT://CONFIG") + android.mid(14)}) {
        const auto profiles = parse(body);
        check(profiles.size() == 1, "Android share link");
        if (profiles.size() != 1) continue;
        const auto *profile = dynamic_cast<Configs::qwdtt *>(profiles.first()->outbound.get());
        check(profile != nullptr, "profile factory");
        if (!profile) continue;
        check(profile->name == "Main (demo)" && profile->password == "a+b &%", "form encoding");
        check(profile->mode == "raw" && profile->hashes.size() == 2, "RAW default and hashes");
        const auto roundTrip = parse(profiles.first()->outbound->ExportToLink().toUtf8());
        check(roundTrip.size() == 1 && roundTrip.first()->outbound->ExportIdentity() == profiles.first()->outbound->ExportIdentity(), "share link round trip");
    }
    const QByteArray excessHashes = "qwdtt://config?name=qWDTT&peer=203.0.113.20%3A56000&hashes=hash1%2Chash2%2Chash3%2Chash4%2Chash5&workers=16&port=9000&pass=test-password";
    const QByteArray excessJson = R"({"profiles":[{"name":"qWDTT","peer":"203.0.113.20:56000","hashes":["hash1","hash2","hash3","hash4","hash5"],"workers":16,"pass":"test-password"}]})";
    for (const auto &body : {excessHashes, excessHashes.toBase64(), excessJson, excessJson.toBase64()}) {
        const auto profiles = parse(body);
        check(profiles.size() == 1, "import five hashes and non-group workers");
        if (profiles.size() != 1) continue;
        const auto *profile = dynamic_cast<Configs::qwdtt *>(profiles.first()->outbound.get());
        check(profile && profile->hashes == QStringList{"hash1", "hash2", "hash3", "hash4"}, "first four hashes in order");
        check(profile && profile->workers == 9 && profile->mode == "raw", "normalize worker count and preserve RAW default");
        const auto roundTrip = parse(profiles.first()->outbound->ExportToLink().toUtf8());
        check(roundTrip.size() == 1 && roundTrip.first()->outbound->ExportIdentity() == profiles.first()->outbound->ExportIdentity(), "normalized import round trip");
    }
    const QByteArray subscription = R"({"subscriptionName":"Demo","profiles":[{"name":"First","peer":"203.0.113.10","pass":"password","hashes":"hash1"},{"name":"Second","peer":"203.0.113.11:56000","password":"password","vkHashes":"hash2","mode":"wg"}]})";
    for (const auto &body : {subscription, subscription.toBase64()}) {
        const auto profiles = parse(body);
        check(profiles.size() == 2, "Android subscription");
        if (profiles.size() != 2) continue;
        check(profiles.first()->outbound->DisplayType() == "qWDTT (RAW)" && profiles.last()->outbound->DisplayType() == "qWDTT (WG)", "subscription modes and ordering");
    }
    check(parse(android + "\n" + android).size() == 2, "text subscription");
    check(parse("qwdtt://config?peer=203.0.113.10%3Ainvalid&pass=test").isEmpty(), "invalid server port");
    check(ReleaseVersion::IsNewer("1.0-beta.2", "1.0-beta.1"), "next beta update");
    check(ReleaseVersion::IsNewer("1.0", "1.0-beta.3"), "beta to stable update");
    check(!ReleaseVersion::IsNewer("1.0-beta.4", "1.0"), "stable cannot downgrade to beta");
    check(!ReleaseVersion::IsNewer("1.0-beta.1", "1.0.0-beta.1"), "optional patch equivalence");
    check(ReleaseVersion::IsNewer("1.1-beta.1", "1.0"), "next minor beta");
    check(!ReleaseVersion::IsNewer("invalid", "1.0"), "invalid update version");
    check(ReleaseVersion::FromAsset("qThrone-1.0-beta.2-windows-amd64.zip") == "1.0-beta.2", "beta asset version");
    check(ReleaseVersion::FromAsset("qThrone-1.0-linux-amd64.tar.gz") == "1.0", "stable asset version");
    qInfo() << "qWDTT import and release version tests:" << failures << "failures";
    return failures == 0 ? 0 : 1;
}
