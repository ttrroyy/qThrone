#include "include/configs/sub/SubscriptionParser.hpp"
#include "include/configs/outbounds/qwdtt.h"
#include <QDebug>

int TestQwdttImport() {
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
    const QByteArray subscription = R"({"subscriptionName":"Demo","profiles":[{"name":"First","peer":"203.0.113.10","pass":"password","hashes":"hash1"},{"name":"Second","peer":"203.0.113.11:56000","password":"password","vkHashes":"hash2","mode":"wg"}]})";
    for (const auto &body : {subscription, subscription.toBase64()}) {
        const auto profiles = parse(body);
        check(profiles.size() == 2, "Android subscription");
        if (profiles.size() != 2) continue;
        check(profiles.first()->outbound->DisplayType() == "qWDTT (RAW)" && profiles.last()->outbound->DisplayType() == "qWDTT (WG)", "subscription modes and ordering");
    }
    check(parse(android + "\n" + android).size() == 2, "text subscription");
    check(parse("qwdtt://config?peer=203.0.113.10%3Ainvalid&pass=test").isEmpty(), "invalid server port");
    qInfo() << "qWDTT import tests:" << failures << "failures";
    return failures == 0 ? 0 : 1;
}
