#include "include/configs/sub/SubscriptionParser.hpp"
#include "include/configs/outbounds/qwdtt.h"
#include <QDebug>
#include "include/global/Version.hpp"
#include "include/ui/profile/edit_qwdtt.h"

int TestQwdttImport() {
    qInstallMessageHandler(nullptr);
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
