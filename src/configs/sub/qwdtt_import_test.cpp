#include "include/configs/sub/SubscriptionParser.hpp"
#include "include/configs/outbounds/qwdtt.h"
#include "include/configs/outbounds/csqtt.h"
#include "include/configs/outbounds/openflux.h"
#include "include/configs/outbounds/olcrtc.h"
#include "include/configs/generate.h"
#include "include/global/Configs.hpp"
#include "include/global/Utils.hpp"
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
#include "include/ui/setting/dialog_vk_relay.h"
#include <QStandardItemModel>
#include "include/ui/profile/edit_csqtt.h"
#include "include/ui/setting/RouteItem.h"
#include <QDialogButtonBox>
#include <QListWidget>
#include <QApplication>
#include <QMainWindow>
#include <QDir>
#include "ui_mainwindow.h"
#include "include/ui/widget/SeasonalDecoration.hpp"
#include "include/ui/setting/dialog_basic_settings.h"

int TestQwdttImport() {
    qInstallMessageHandler(nullptr);
    const auto previousLogger = MW_show_log;
    MW_show_log = [](const QString &message) { qWarning().noquote() << message; };
    const auto restoreLogger = qScopeGuard([previousLogger] { MW_show_log = previousLogger; });
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
    {
        const auto previousCallback = MW_dialog_message;
        QStringList flags;
        MW_dialog_message = [&flags](MwMessage, QStringList changes) { flags = changes; };
        const auto restoreCallback = qScopeGuard([previousCallback] { MW_dialog_message = previousCallback; });
        DialogBasicSettings dialog;
        auto *selector = dialog.findChild<QComboBox *>("seasonal_decoration");
        check(selector && selector->count() == 3, "exactly Disabled, Halloween and New Year options");
        if (selector) {
            selector->setCurrentIndex(selector->findData("newyear"));
            const auto inboundBefore = Configs::dataManager->settingsRepo->inbound_socks_port;
            dialog.accept();
            check(flags == QStringList{QStringLiteral("decorationOnly")}, "decoration change uses live appearance update");
            check(Configs::dataManager->settingsRepo->inbound_socks_port == inboundBefore, "decoration change preserves network settings");
        }
    }
    {
        // Exercise the real .ui hierarchy: a splitter silently adopts direct QWidget children.
        QMainWindow window;
        Ui::MainWindow ui;
        ui.setupUi(&window);
        auto *filter = new QToolButton(&window);
        filter->setIcon(QIcon(":/icon/filter.png"));
        ui.tabWidget->setCornerWidget(filter, Qt::TopRightCorner);
        const int panes = ui.splitter->count();
        auto *decoration = installSeasonalDecoration(ui.centralwidget, ui.horizontalLayout_2, ui.tabWidget->tabBar(), filter);
        const QString screenshots = qEnvironmentVariable("THRONE_SEASONAL_PREVIEW_DIR");
        if (!screenshots.isEmpty()) QDir().mkpath(screenshots);
        for (bool dark : {false, true}) {
            auto palette = window.palette();
            palette.setColor(QPalette::Window, dark ? QColor("#202020") : QColor("#f2f2f2"));
            palette.setColor(QPalette::WindowText, dark ? Qt::white : Qt::black);
            window.setPalette(palette);
            for (const QSize size : {QSize(800, 600), QSize(1152, 720), QSize(1600, 1000)}) {
                window.resize(size);
                for (const QString mode : {QStringLiteral("halloween"), QStringLiteral("newyear"), QStringLiteral("none"), QStringLiteral("halloween")}) {
                    const QSize previousGeometry = decoration->size();
                    decoration->setDecoration(mode);
                    window.show();
                    for (int i = 0; i < 4; ++i) QApplication::processEvents();
                    if (mode == "newyear") check(decoration->size() == previousGeometry, "both decorations share the same dimensions");
                    if (mode != "none") check(decoration->size() == QSize(190, 64), "artwork keeps fixed size while resizing");
                    check(ui.splitter->count() == panes, "seasonal overlay never becomes a splitter pane");
                    check(ui.splitter->width() >= ui.centralwidget->width() - 24, "seasonal overlay preserves full-width panels");
                    check(ui.tabWidget->width() >= ui.splitter->width() - 24, "profile table preserves panel width");
                    check(decoration->parentWidget() == ui.centralwidget, "decoration belongs to central overlay container");
                    check(decoration->isVisible() == (mode != "none" && size.width() >= 1130), "decoration hides instead of shrinking when space is insufficient");
                }
                decoration->setDecoration("newyear");
                for (int i = 0; i < 6; ++i) QApplication::processEvents();
                if (!screenshots.isEmpty()) check(window.grab().save(screenshots + QString("/newyear-%1-%2.png").arg(dark ? "dark" : "light").arg(size.width())), "newyear screenshot saved");
                decoration->setNoticePriority(true);
                ui.data_view->setHtml("<p>Settings changed, restart to apply</p><a href='restart'>Restart</a> · <a href='dismiss'>Dismiss</a>");
                window.move(window.pos() + QPoint(17, 11));
                for (int i = 0; i < 6; ++i) QApplication::processEvents();
                check(!decoration->isVisible(), "restart notice takes priority over artwork");
                check(ui.data_view->width() > 100, "notice retains available width after move");
                check(ui.splitter->count() == panes, "moving window preserves splitter panes");
                if (!screenshots.isEmpty()) check(window.grab().save(screenshots + QString("/notice-%1-%2.png").arg(dark ? "dark" : "light").arg(size.width())), "notice screenshot saved");
                decoration->setNoticePriority(false);
                decoration->setDecoration("halloween");
                ui.data_view->clear();
                for (int i = 0; i < 6; ++i) QApplication::processEvents();
                if (!screenshots.isEmpty()) check(window.grab().save(screenshots + QString("/seasonal-%1-%2.png").arg(dark ? "dark" : "light").arg(size.width())), "seasonal window screenshot saved");
            }
        }
    }
    check(SaveVKRelayPreviews(qEnvironmentVariable("THRONE_SEASONAL_PREVIEW_DIR")), "VK relay screenshots saved");
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
    if (!fixtureGroups.isEmpty() && !testProfiles.isEmpty() && !csProfiles.isEmpty()) {
        auto cs = csProfiles.first();
        check(Configs::dataManager->profilesRepo->AddProfile(cs, fixtureGroups.first()), "save routed CSQTT fixture");
        const auto originalDevice = dynamic_cast<Configs::csqtt*>(cs->outbound.get())->deviceID;
        auto refreshed = Configs::ProfilesRepo::NewProfile("csqtt");
        auto changed = cs->outbound->ExportToJson();
        changed.remove("device_id"); changed["tag"] = "Renamed subscription profile";
        changed["hashes"] = QJsonArray{"abcdefghijklmnop-new-call"};
        check(refreshed->outbound->ParseFromJson(changed), "updated CSQTT subscription parses");
        Configs::dataManager->profilesRepo->RestoreTransportIdentity(refreshed.get());
        check(dynamic_cast<Configs::csqtt*>(refreshed->outbound.get())->deviceID == originalDevice,
              "CSQTT identity survives name and call-hash changes in a subscription");
        auto mains = parse("vless://00000000-0000-4000-8000-000000000001@203.0.113.12:443?type=tcp&security=none#Routing-main");
        check(mains.size() == 1, "parse main VLESS fixture");
        if (mains.size() == 1) {
            auto main = mains.first();
            check(Configs::dataManager->profilesRepo->AddProfile(main, fixtureGroups.first()), "save main VLESS fixture");
            auto route = std::make_shared<Configs::RouteProfile>();
            route->name = "Multiple tunnel outputs";
            auto domain = std::make_shared<Configs::RouteRule>();
            domain->outboundID = cs->id;
            domain->domain_suffix = {"example.invalid"};
            auto application = std::make_shared<Configs::RouteRule>();
            application->outboundID = testProfiles.first()->id;
            application->process_name = {"routing-test.exe"};
            route->Rules = {domain, application};
            {
                RouteItem editor(nullptr, route);
                auto *rows = editor.findChild<QListWidget *>("route_items");
                check(rows != nullptr, "routing editor exposes saved rules");
                if (rows) rows->setCurrentRow(0);
                auto *choice = editor.findChild<QComboBox *>("ruleOutbound");
                check(choice && choice->currentData().toInt() == cs->id,
                      "routing editor restores profile by id with group-prefixed labels");
                if (choice) choice->setCurrentIndex(choice->findData(testProfiles.first()->id));
                check(editor.chain->Rules.first()->outboundID == testProfiles.first()->id,
                      "routing editor changes selected profile id");
                auto *buttons = editor.findChild<QDialogButtonBox *>("buttonBox");
                if (buttons) buttons->accepted();
                RouteItem reopened(nullptr, editor.chain);
                auto *reopenedRows = reopened.findChild<QListWidget *>("route_items");
                if (reopenedRows) reopenedRows->setCurrentRow(0);
                auto *restored = reopened.findChild<QComboBox *>("ruleOutbound");
                check(restored && restored->currentData().toInt() == testProfiles.first()->id,
                      "routing editor preserves selected profile after save and reopen");
            }
            check(Configs::dataManager->routesRepo->AddRouteProfile(route), "save mixed transport routing fixture");
            auto *settings = Configs::dataManager->settingsRepo.get();
            const auto oldRoute = settings->current_route_id;
            settings->current_route_id = route->id;
            const auto restoreRoute = qScopeGuard([settings, oldRoute] { settings->current_route_id = oldRoute; });
            const bool oldTun = settings->spmode_vpn, oldIPv6 = settings->vpn_ipv6;
            settings->spmode_vpn = true; settings->vpn_ipv6 = true;
            const auto restoreTun = qScopeGuard([settings, oldTun, oldIPv6] { settings->spmode_vpn = oldTun; settings->vpn_ipv6 = oldIPv6; });
            const auto mixed = Configs::BuildSingBoxConfig(main);
            check(mixed->error.isEmpty() && mixed->routedExtraCores.size() == 2,
                  "VLESS with separate qWDTT and CSQTT routing outputs builds");
            for (const auto &entry : mixed->coreConfig["inbounds"].toArray()) {
                const auto inbound = entry.toObject();
                if (inbound["type"] == "tun") check(inbound["address"].toArray().size() == 2, "mixed routing preserves IPv6 on the TUN");
            }
            const auto familyRules = mixed->coreConfig["dns"].toObject()["rules"].toArray();
            check(!familyRules.isEmpty() && familyRules.first().toObject()["domain_suffix"].toArray() == QJsonArray{"example.invalid"}
                  && familyRules.first().toObject()["action"] == "predefined", "CSQTT AAAA suppression is scoped to its routed domains");
            for (const auto &transport : mixed->routedExtraCores) {
                bool matched = false;
                const auto config = QJsonDocument::fromJson(transport.config.toUtf8()).object();
                const auto port = config["socks"].toString().section(':', -1).toInt();
                for (const auto &value : mixed->coreConfig["outbounds"].toArray()) {
                    const auto outbound = value.toObject();
                    if (outbound["tag"] == transport.outboundTag) {
                        matched = outbound["type"] == "socks" && outbound["server_port"].toInt() == port &&
                                  outbound["password"] == config["socks_pass"];
                    }
                }
                check(matched, "routing outbound points at its own authenticated transport bridge");
            }
            const auto reused = Configs::BuildSingBoxConfig(testProfiles.first());
            check(reused->error.isEmpty() && reused->routedExtraCores.size() == 1 &&
                  !reused->extraCoreData->path.isEmpty(),
                  "main tunnel referenced in a routing rule starts only once");
        }
    }
    const auto rtcKey = QString(64, 'a');
    for (const auto &transport : QStringList{"datachannel", "vp8channel", "seichannel", "videochannel"}) {
        const auto link = "olcrtc://jitsi?" + transport + "@https://example.org/room#" + rtcKey + "$RTC";
        auto rtcProfiles = parse(link.toUtf8());
        check(rtcProfiles.size() == 1, "olcRTC transports import");
        if (rtcProfiles.isEmpty() || fixtureGroups.isEmpty()) continue;
        auto rtcProfile = rtcProfiles.first();
        auto *rtc = dynamic_cast<Configs::olcrtc*>(rtcProfile->outbound.get());
        check(rtc && parse(rtc->ExportToLink().toUtf8()).size() == 1, "olcRTC URI round trip");
        check(Configs::dataManager->profilesRepo->AddProfile(rtcProfile, fixtureGroups.first()), "save olcRTC fixture");
        const auto stableDevice = rtc->deviceID;
        auto renewed = parse((link + "-new-name").toUtf8()).first();
        Configs::dataManager->profilesRepo->RestoreTransportIdentity(renewed.get());
        check(dynamic_cast<Configs::olcrtc*>(renewed->outbound.get())->deviceID == stableDevice, "olcRTC subscription refresh preserves device across name and transport changes");
        const auto probe = Configs::BuildTestConfig({rtcProfile});
        check(probe->error.isEmpty() && probe->qwdttConfigs.size() == 1, "olcRTC ping and speed private bridge");
        const auto live = Configs::BuildSingBoxConfig(rtcProfile);
        check(live->error.isEmpty() && live->extraCoreData->args.startsWith("--qthrone-config"), "olcRTC main bundled helper");
        check(rtc->Build().object["network"].toString() == "tcp", "olcRTC bridge cannot silently send UDP direct");
        auto mains = parse("vless://00000000-0000-4000-8000-000000000002@203.0.113.15:443?type=tcp&security=none#RTC-main");
        if (mains.size() == 1) {
            auto main = mains.first(); Configs::dataManager->profilesRepo->AddProfile(main, fixtureGroups.first());
            auto route = std::make_shared<Configs::RouteProfile>(); route->name = "RTC domain route";
            auto rule = std::make_shared<Configs::RouteRule>(); rule->domain_suffix = {"rtc.example.invalid"}; rule->outboundID = rtcProfile->id; route->Rules = {rule};
            Configs::dataManager->routesRepo->AddRouteProfile(route);
            auto *settings = Configs::dataManager->settingsRepo.get(); const auto oldRoute = settings->current_route_id; settings->current_route_id = route->id;
            const auto restoreRoute = qScopeGuard([settings,oldRoute] {settings->current_route_id = oldRoute;});
            const auto mixed = Configs::BuildSingBoxConfig(main);
            check(mixed->error.isEmpty() && mixed->routedExtraCores.size() == 1, "VLESS main with separately routed olcRTC");
            if (!mixed->routedExtraCores.isEmpty()) check(mixed->routedExtraCores.first().config.contains(stableDevice), "routed olcRTC uses persistent device identity");
            const auto reused = Configs::BuildSingBoxConfig(rtcProfile);
            check(reused->error.isEmpty() && reused->routedExtraCores.isEmpty(), "olcRTC main reused by own routing rule");
        }
    }
    check(parse(("olcrtc://telemost?datachannel@room#" + rtcKey).toUtf8()).isEmpty(), "reject unsupported Telemost DataChannel");
    check(parse("olcrtc://jitsi?datachannel@https://example.org/room#bad").isEmpty(), "reject invalid olcRTC key");
    Configs::openflux flux;
    const QJsonObject fluxConfig{{"type", "openflux"}, {"name", "Documents"}, {"secret", "test-secret-123456789"}, {"context", "https://docs.yandex.ru/demo"}, {"negotiate", true},
        {"transports", QJsonArray{QJsonObject{{"type", "direct"}, {"priority", 100}, {"dial", "203.0.113.11:18445"}},
                                  QJsonObject{{"type", "yandex"}, {"priority", 50}, {"url", "https://docs.yandex.ru/demo"}},
                                  QJsonObject{{"type", "mailru"}, {"priority", 40}, {"url", "https://docs.mail.ru/demo"}}}}};
    check(flux.ParseFromJson(fluxConfig), "OpenFlux document and direct transports parse");
    const auto fluxProfiles = parse(flux.ExportToLink().toUtf8());
    check(fluxProfiles.size() == 1 && fluxProfiles.first()->type == "openflux" && fluxProfiles.first()->outbound->ExportIdentity() == flux.ExportIdentity(), "OpenFlux native compressed share link round trip");
    if (!fluxProfiles.isEmpty() && !fixtureGroups.isEmpty()) {
        auto profile = fluxProfiles.first();
        check(Configs::dataManager->profilesRepo->AddProfile(profile, fixtureGroups.first()), "save OpenFlux fixture");
        const auto probe = Configs::BuildTestConfig({profile});
        check(probe->error.isEmpty() && probe->qwdttConfigs.size() == 1 && probe->fullConfigs.size() == 1, "OpenFlux ping and speed tests own a private bridge");
        const auto live = Configs::BuildSingBoxConfig(profile);
        check(live->error.isEmpty() && live->extraCoreData->args.startsWith("--qthrone-config"), "OpenFlux main connection uses bundled helper");
    }
    auto invalidFlux = fluxConfig; invalidFlux["secret"] = "short";
    check(!flux.ParseFromJson(invalidFlux), "OpenFlux rejects short secrets");
    check(!flux.ParseFromLink("openflux://v1/not-a-valid-payload"), "OpenFlux rejects malformed compressed links");
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
    {
        auto *settings = Configs::dataManager->settingsRepo.get();
        settings->qwdtt_hash_source = "manual"; settings->vk_relay_linked = false;
        EditQWDTT editor;
        auto *source = editor.findChild<QComboBox *>("qwdttHashSource");
        check(source && source->currentData() == "manual", "manual hash source by default");
        check(source && !qobject_cast<QStandardItemModel *>(source->model())->item(1)->isEnabled(), "automatic hashes disabled without linked VK");
        if (source) {
            qobject_cast<QStandardItemModel *>(source->model())->item(1)->setEnabled(true);
            source->setCurrentIndex(1);
            for (int i = 1; i <= 4; ++i) check(editor.findChild<QLineEdit *>(QString("qwdttHash%1").arg(i))->isHidden(), "automatic source hides manual hashes");
            source->setCurrentIndex(0);
            for (int i = 1; i <= 4; ++i) check(!editor.findChild<QLineEdit *>(QString("qwdttHash%1").arg(i))->isHidden(), "manual source restores hash inputs");
        }
        Configs::qwdtt automatic;
        check(automatic.ParseFromLink("qwdtt://config?peer=203.0.113.10:56000&pass=test&hash_source=auto&auto_hash_count=4&workers=108"), "automatic hash profile import");
        check(automatic.hashSource == "auto" && automatic.autoHashCount == 4 && automatic.workers == 108, "automatic hash count controls workers");
        Configs::qwdtt roundTrip;
        check(roundTrip.ParseFromLink(automatic.ExportToLink()) && roundTrip.ExportIdentity() == automatic.ExportIdentity(), "automatic profile round trip");
        settings->vk_relay_linked = true; settings->qwdtt_hash_source = "auto";
        auto profile = Configs::ProfilesRepo::NewProfile("qwdtt");
        check(dynamic_cast<Configs::qwdtt *>(profile->outbound.get())->hashSource == "auto", "new qWDTT inherits selected default");
        Configs::qwdtt deployed;
        check(deployed.ParseFromLink("qwdtt://config?peer=203.0.113.10:56000&pass=test&device_id=deployment"), "deployment profile imports without manual hashes");
        check(deployed.hashSource == "auto", "deployment profile inherits automatic default");
        Configs::dataManager->profilesRepo->AddProfile(profile, fixtureGroups.first());
        check(ResetQWDTTHashSources(), "unlink saves reset hash sources");
        check(dynamic_cast<Configs::qwdtt *>(profile->outbound.get())->hashSource == "manual", "unlink resets existing automatic profile");
        check(settings->qwdtt_hash_source == "manual" && !settings->vk_relay_linked, "unlink resets global default");
    }
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
