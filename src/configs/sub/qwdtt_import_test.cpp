#include "include/configs/sub/SubscriptionParser.hpp"
#include "include/configs/sub/ProviderRouting.h"
#include "include/configs/sub/GroupUpdater.hpp"
#include "include/ui/group/dialog_provider_routing.h"
#include "include/ui/utils/ProfilesTableFilterHeader.h"
#include "include/ui/utils/DataViewHtmlGenerator.h"
#include "include/ui/utils/TestResultDelegate.hpp"
#include "include/ui/widget/RestartNotice.hpp"
#include <QStandardItemModel>
#include "include/configs/outbounds/qwdtt.h"
#include "include/configs/outbounds/csqtt.h"
#include "include/configs/outbounds/wdttplus.h"
#include "include/configs/outbounds/openflux.h"
#include "include/configs/outbounds/olcrtc.h"
#include "include/configs/outbounds/dnstunnel.h"
#include "include/configs/outbounds/s3bypass.h"
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
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QScopeGuard>
#include "include/global/Version.hpp"
#include "NkrVersion.h"
#include "include/ui/profile/edit_qwdtt.h"
#include "include/ui/setting/dialog_vk_relay.h"
#include <QStandardItemModel>
#include "include/ui/profile/edit_csqtt.h"
#include "include/ui/profile/edit_wdttplus.h"
#include "include/ui/utils/ShareBlocks.h"
#include "3rdparty/QrDecoder.h"
#include "include/ui/setting/RouteItem.h"
#include <QDialogButtonBox>
#include <QListWidget>
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <QMainWindow>
#include <QDir>
#include "ui_mainwindow.h"
#include "include/ui/widget/SeasonalDecoration.hpp"
#include "include/ui/setting/dialog_basic_settings.h"
#include "include/configs/outbounds/sstp.h"

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
    {
        auto *settings = Configs::dataManager->settingsRepo.get();
        const auto savedAgent = settings->user_agent;
        const auto restoreAgent = qScopeGuard([&] { settings->user_agent = savedAgent; });
        settings->user_agent.clear();
        if (settings->GetUserAgent() != QStringLiteral("qThrone/") + QStringLiteral(NKR_VERSION) ||
            Subscription::ResolveIdentity(nullptr).userAgent != QStringLiteral("Happ/3.0 qThrone/") + QStringLiteral(NKR_VERSION)) {
            qWarning("Client/subscription User-Agent does not match the build version"); ++failures;
        }
        settings->user_agent = "Custom/agent";
        if (settings->GetUserAgent() != "Custom/agent" || Subscription::ResolveIdentity(nullptr).userAgent != "Custom/agent") {
            qWarning("Custom User-Agent not preserved"); ++failures;
        }
    }
    const auto check = [&failures](bool ok, const char *name) {
        if (!ok) { qCritical() << "qWDTT import test failed:" << name; ++failures; }
    };
    {
        auto *settings = Configs::dataManager->settingsRepo.get();
        const auto savedTheme = settings->seasonal_decoration;
        const auto savedMigration = settings->seasonal_auto_13_migrated;
        const auto restoreSettings = qScopeGuard([&] {
            settings->seasonal_decoration = savedTheme;
            settings->seasonal_auto_13_migrated = savedMigration;
            settings->Save();
        });
        settings->seasonal_decoration = "halloween";
        settings->seasonal_auto_13_migrated = false;
        settings->Save();
        Configs::SettingsRepo upgraded(Configs::dataManager->getDatabase());
        check(upgraded.seasonal_decoration == "auto" && upgraded.seasonal_auto_13_migrated,
              "1.3 upgrade selects Auto once");
        for (const auto &choice : {QStringLiteral("christmas"), QStringLiteral("none")}) {
            upgraded.seasonal_decoration = choice;
            upgraded.Save();
            Configs::SettingsRepo reopened(Configs::dataManager->getDatabase());
            check(reopened.seasonal_decoration == choice && reopened.seasonal_auto_13_migrated,
                  "subsequent launches and upgrades preserve manual or disabled selection");
        }
    }
    {
        const auto theme = SeasonalDecoration::themeForDate;
        for (int year : {2026, 2027, 2028}) {
            for (QDate date(year, 1, 1); date.year() == year; date = date.addDays(1)) {
                check(!theme(date).isEmpty(), "Auto covers every local calendar day, including leap day");
            }
            for (int month = 1; month <= 12; ++month) {
                const QString expected = month <= 2 || month == 12 ? "newyear" : month <= 5 ? "easter" : month <= 8 ? "kupala" : "halloween";
                check(theme(QDate(year, month, 1)) == expected && theme(QDate(year, month, QDate(year, month, 1).daysInMonth())) == expected,
                      "Auto follows the four seasons on both month boundaries");
            }
        }
        QFile resource(":/seasonal/manifest.json");
        check(resource.open(QIODevice::ReadOnly), "seasonal manifest bundled");
        const auto manifest = QJsonDocument::fromJson(resource.readAll()).object();
        check(manifest.size() == 4, "only four seasonal themes bundled");
        for (auto themeIt = manifest.begin(); themeIt != manifest.end(); ++themeIt) {
            check(themeIt.value().toObject().size() == 3, "wide, compact and narrow compositions bundled");
            for (const auto &variant : {QStringLiteral("wide"), QStringLiteral("compact"), QStringLiteral("narrow")}) {
                const auto metadata = themeIt.value().toObject()[variant].toObject();
                const QImage image(":/seasonal/" + metadata["file"].toString());
                check(!image.isNull() && image.hasAlphaChannel() && metadata["frames"].toInt() > 1, "approved transparent animated composition bundled");
            }
        }
    }
    {
        const auto previousCallback = MW_dialog_message;
        QStringList flags;
        MW_dialog_message = [&flags](MwMessage, QStringList changes) { flags = changes; };
        const auto restoreCallback = qScopeGuard([previousCallback] { MW_dialog_message = previousCallback; });
        DialogBasicSettings dialog;
        auto *selector = dialog.findChild<QComboBox *>("seasonal_decoration");
        check(selector && selector->count() == 6 && selector->findData("auto") >= 0 && selector->findData("remembrance") < 0 && selector->findData("may_remembrance") < 0, "four manual seasons, Auto and Disabled");
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
                for (const QString &mode : SeasonalDecoration::manualThemes() + QStringList{"none", "halloween"}) {
                    const QSize previousGeometry = decoration->size();
                    decoration->setDecoration(mode);
                    window.show();
                    for (int i = 0; i < 4; ++i) QApplication::processEvents();
                    if (mode == "newyear") check(decoration->size() == previousGeometry, "both decorations share the same dimensions");
                    if (mode != "none") check(decoration->hasArtwork(), "approved artwork resource loads");
                    if (!screenshots.isEmpty() && size.width() == 1152 && dark && mode != "none") check(window.grab().save(screenshots + "/theme-" + mode + ".png"), "theme preview saved");
                    if (mode != "none") check(decoration->width() > 0 && decoration->width() <= 190 && decoration->height() <= 64, "artwork scales within the available toolbar space");
                    check(ui.splitter->count() == panes, "seasonal overlay never becomes a splitter pane");
                    check(ui.splitter->width() >= ui.centralwidget->width() - 24, "seasonal overlay preserves full-width panels");
                    check(ui.tabWidget->width() >= ui.splitter->width() - 24, "profile table preserves panel width");
                    check(decoration->parentWidget() == ui.centralwidget, "decoration belongs to central overlay container");
                    check(decoration->isVisible() == (mode != "none"), "decoration remains visible in narrow windows");
                }
                decoration->setDecoration("newyear");
                for (int i = 0; i < 6; ++i) QApplication::processEvents();
                if (!screenshots.isEmpty()) check(window.grab().save(screenshots + QString("/newyear-%1-%2.png").arg(dark ? "dark" : "light").arg(size.width())), "newyear screenshot saved");
                decoration->setNoticePriority(true);
                ui.data_view->setHtml("<p>Settings changed, restart to apply</p><a href='restart'>Restart</a> В· <a href='dismiss'>Dismiss</a>");
                window.move(window.pos() + QPoint(17, 11));
                for (int i = 0; i < 6; ++i) QApplication::processEvents();
                if (size.width() == 1600) check(decoration->isVisible(), "short notice and artwork coexist when there is room");
                ui.data_view->setHtml("<p style='white-space:pre'>" + QString(1000, 'W') + "</p>");
                decoration->setNoticePriority(true);
                for (int i = 0; i < 6; ++i) QApplication::processEvents();
                check(!decoration->isVisible(), "notice hides artwork only when it cannot fit alongside");
                ui.data_view->setHtml("<p>Settings changed, restart to apply</p><a href='restart'>Restart</a> · <a href='dismiss'>Dismiss</a>");
                decoration->setNoticePriority(true);
                for (int i = 0; i < 6; ++i) QApplication::processEvents();
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
    check(SaveProviderRoutingPreviews(qEnvironmentVariable("THRONE_SEASONAL_PREVIEW_DIR")), "provider routing screenshots saved");
    const QByteArray android = "qwdtt://config?name=Main+%28demo%29&peer=203.0.113.10%3A56000&hashes=hash1%2Chash2&workers=9&port=9000&pass=a%2Bb+%26%25";
    const auto parse = [](const QByteArray &body) {
        QList<std::shared_ptr<Configs::Profile>> profiles;
        Subscription::ParseSink sink;
        sink.profile = [&profiles](auto profile) { profiles.append(profile); };
        Subscription::ParseDocument(body, sink);
        return profiles;
    };
    {
        const auto link = QStringLiteral("sstp://user%40example:p%3A%2B%2F%40ss@[2001:db8::1]:444?sni=vpn.example.org&insecure=1#SSTP%20test");
        Configs::sstp profile, restored;
        check(profile.ParseFromLink(link) && profile.server == "2001:db8::1" && profile.server_port == 444 &&
              profile.username == "user@example" && profile.password == "p:+/@ss" && profile.allowInsecure, "SSTP imports escaped credentials, IPv6, SNI and TLS options");
        check(restored.ParseFromLink(profile.ExportToLink()) && restored.ExportToJson() == profile.ExportToJson(), "SSTP URI round trip");
        check(restored.ParseFromJson(profile.ExportToJson()) && restored.ExportIdentity() == profile.ExportIdentity(), "SSTP JSON round trip");
        check(parse(link.toUtf8()).size() == 1 && parse(QJsonDocument(profile.ExportToJson()).toJson()).size() == 1, "SSTP clipboard/file and subscription dispatch");
        check(!restored.ParseFromLink("sstp://user:pass@vpn.example.org:0") && !restored.ParseFromLink("sstp://user@vpn.example.org"), "SSTP rejects invalid ports and missing credentials");
        const auto challenge = QByteArray("{\"outbounds\":[{\"server\":\"127.0.0.1\",\"tag\":\"HWID missing\"}]}");
        check(Subscription::RequiresHwid(challenge) && Subscription::RequiresHwid(challenge.toBase64()), "HWID-required placeholder is recognized before import");
        const auto encodedChallenge = QByteArray("vless://00000000-0000-0000-0000-000000000001@127.0.0.1:1#") + QUrl::toPercentEncoding(QStringLiteral("Не получен HWID устройства"));
        check(Subscription::RequiresHwid(encodedChallenge) && Subscription::RequiresHwid(encodedChallenge.toBase64()), "HWID challenge in an encoded profile name is recognized");
        check(!Subscription::RequiresHwid("{\"server\":\"192.0.2.1\",\"tag\":\"HWID enabled\"}"), "valid HWID-enabled profiles are not treated as challenges");
        Configs::Group disabled;
        disabled.sub_options.send_hwid = Configs::sendHwid::off;
        check(!Subscription::ResolveIdentity(&disabled).allowHwidRetry, "explicit per-subscription HWID opt-out is preserved");
    }
    {
        namespace PR = Subscription::ProviderRouting;
        const QJsonObject document{{"Name", "Provider route"}, {"GlobalProxy", false}, {"LastUpdated", 10}, {"RouteOrder", "direct-proxy-block"}, {"DirectSites", QJsonArray{"full:example.org", "geosite:WHITELIST"}}, {"ProxyIp", QJsonArray{"192.0.2.1"}}, {"UnknownProviderField", "preserved"}};
        const auto link = QStringLiteral("happ://routing/onadd/") + QString::fromLatin1(QJsonDocument(document).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
        PR::Command command; QString error;
        check(PR::Parse(link, command, error) && command.activate, "Happ routing command parses");
        auto incyLink = link; incyLink.replace("happ://", "incy://");
        check(PR::Parse(incyLink, command, error) && command.activate && command.provider == "Incy", "Incy routing command preserves routing document");
        check(PR::Parse(link, command, error), "Happ routing restores provider identity");
        const auto wrapped = "https://example.org/launch?url=" + QString::fromLatin1(QUrl::toPercentEncoding(link));
        check(PR::IsLink(wrapped) && PR::Parse(wrapped, command, error), "wrapped Happ routing link decodes locally");
        const QByteArray server = "vless://00000000-0000-0000-0000-000000000001@192.0.2.10:443?security=tls#Server";
        const auto payload = PR::Extract((link.toUtf8() + '\n' + server).toBase64());
        check(payload.commands.size() == 1 && payload.body == server && payload.warnings.isEmpty(), "base64 subscription keeps servers and extracts routing");
        const auto broken = PR::Extract(server, "happ://routing/add/not-json");
        check(broken.body == server && broken.commands.isEmpty() && !broken.warnings.isEmpty(), "broken routing does not drop servers");
        Configs::SubscriptionOptions options;
        PR::Apply(options, payload.commands);
        check(options.provider_routing.contains("UnknownProviderField"), "provider unknown fields preserved");
        const auto roundtrip = Configs::SubscriptionOptions::FromJson(options.ToJson());
        check(roundtrip.provider_routing == options.provider_routing && roundtrip.provider_routing_enabled, "subscription routing persistence");
        auto older = document; older["LastUpdated"] = 1; older["Name"] = "Provider route"; older["GlobalProxy"] = true;
        PR::Apply(options, {PR::Command{"Happ", true, false, older}});
        check(!QJsonDocument::fromJson(options.provider_routing.toUtf8()).object().value("GlobalProxy").toBool(), "older route cannot replace newer route");
        const auto profile = PR::Profile(document, "Happ");
        check(profile->defaultOutboundID == Configs::directID && profile->Rules.size() == 3 && profile->Rules[1]->rule_set.contains("geosite:WHITELIST") && profile->Rules[2]->ip_cidr.contains("192.0.2.1/32"), "provider rule order and IP normalization");
        {
            Configs::SubscriptionOptions separate;
            auto happ = document; auto incy = document; incy["DirectSites"] = QJsonArray{"domain:other.example"};
            PR::Apply(separate, {PR::Command{"Happ", true, false, happ}, PR::Command{"Incy", true, false, incy}});
            const auto saved = QJsonDocument::fromJson(separate.provider_routing_profiles.toUtf8()).array();
            check(saved.size() == 2 && PR::Identity(saved[0].toObject()) != PR::Identity(saved[1].toObject()), "same-name Happ and Incy originals remain separate");
            check(!PR::SameSettings(saved[0].toObject(), saved[1].toObject()), "different settings remain distinct choices");
            check(PR::SameSettings(happ, document), "identical settings group into one choice");
            auto renamed = happ; renamed["Name"] = "Different name"; renamed["LastUpdated"] = 999;
            check(PR::SameSettings(happ, renamed), "choice grouping ignores name and timestamp");
            separate.provider_routing = QJsonDocument(saved[0].toObject()).toJson(QJsonDocument::Compact);
            separate.routing_provider = "Happ"; separate.provider_routing_manual = true;
            PR::Apply(separate, {PR::Command{"Incy", true, false, incy}});
            check(separate.routing_provider == "Happ", "subscription onadd preserves explicit manual selection");
        }
        options.provider_ignore_import = true; const auto previous = options.provider_routing;
        PR::Apply(options, {PR::Command{"Happ", false, true, {}}});
        check(options.provider_routing == previous, "disabled route import preserves selection");
        const QByteArray xray = R"({"remarks":"Reality server","outbounds":[{"protocol":"vless","tag":"proxy","settings":{"vnext":[{"address":"example.net","port":443,"users":[{"id":"00000000-0000-0000-0000-000000000001"}]}]}}],"routing":{"rules":[{"type":"field","domain":["geosite:WHITELIST"],"outboundTag":"proxy"}]}})";
        const auto profiles = parse(xray);
        check(profiles.size() == 1 && profiles.first()->Custom() && profiles.first()->Custom()->IsXrayFullConfig(), "single full Xray object preserves routing");
        if (!profiles.isEmpty() && profiles.first()->Custom()) {
            check(profiles.first()->outbound->DisplayType() == "VLESS (Xray)" && profiles.first()->outbound->DisplayAddress() == "example.net:443", "full Xray profile displays actual protocol and address");
            check(profiles.first()->Custom()->config.contains("WHITELIST"), "full Xray import keeps provider category");
        }
        const auto balanced = parse(R"([{"remarks":"Auto","outbounds":[{"protocol":"vless","tag":"server1","settings":{"vnext":[{"address":"example.net","port":443,"users":[{"id":"00000000-0000-0000-0000-000000000001"}]}]},"streamSettings":{"sockopt":{"dialerProxy":"front"}}}],"routing":{"balancers":[{"tag":"auto","selector":["server"],"strategy":{"type":"leastPing"}}],"rules":[{"type":"field","balancerTag":"auto","network":"tcp,udp"}]},"observatory":{"subjectSelector":["server"]}}])");
        check(balanced.size() == 1 && balanced.first()->Custom() && balanced.first()->outbound->DisplayType() == "VLESS (Xray)" && balanced.first()->Custom()->config.contains("dialerProxy") && balanced.first()->Custom()->config.contains("observatory"), "subscription displays actual protocol and preserves Xray balancers and chain settings");
        auto group = Configs::GroupsRepo::NewGroup(); group->name = "Provider test";
        auto isolated = document; isolated["DirectSites"] = QJsonArray{"full:example.org"}; isolated["FakeDNS"] = false;
        PR::Apply(group->sub_options, {PR::Command{"Happ", true, false, isolated}});
        check(Configs::dataManager->groupsRepo->AddGroup(group), "provider test group saved");
        auto socks = Configs::ProfilesRepo::NewProfile("socks"); socks->outbound->ParseFromJson(QJsonObject{{"type","socks"},{"server","192.0.2.8"},{"server_port",1080}});
        check(Configs::dataManager->profilesRepo->AddProfile(socks, group->id), "provider test profile saved");
        const auto globalRoute = Configs::dataManager->settingsRepo->current_route_id;
        const auto globalDNS = Configs::dataManager->settingsRepo->remote_dns;
        const auto built = Configs::BuildSingBoxConfig(socks);
        check(built->error.isEmpty() && built->coreConfig.value("route").toObject().value("final").toString() == "direct", "subscription route applies only to its group");
        check(Configs::dataManager->settingsRepo->current_route_id == globalRoute && Configs::dataManager->settingsRepo->remote_dns == globalDNS, "provider routing does not mutate global routing or DNS");
        const auto sourceURL = QStringLiteral("https://example.invalid/geo-fixture/") + fixture.path();
        const auto cache = Configs::GetBasePath() + "/provider-geodata/" + QString::fromLatin1(QCryptographicHash::hash(sourceURL.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".dat";
        QDir().mkpath(QFileInfo(cache).absolutePath());
        QFile cached(cache); check(cached.open(QIODevice::WriteOnly), "Geo fixture cache opens"); cached.write(QByteArray::fromHex("0a0b0a0957484954454c495354")); cached.close();
        const auto cleanCache = qScopeGuard([cache] { QFile::remove(cache); });
        QJsonObject geoConfig{{"routing", QJsonObject{{"rules", QJsonArray{QJsonObject{{"domain", QJsonArray{"geosite:WHITELIST"}}}}}}}, {"dns", QJsonObject{{"hosts", QJsonObject{{"geosite:WHITELIST", "192.0.2.2"}}}}}};
        Configs::SubscriptionOptions geoOptions;
        PR::Apply(geoOptions, {PR::Command{"Happ", true, false, QJsonObject{{"Name", "Provider"}, {"Geositeurl", sourceURL}}}});
        geoOptions.provider_routing_enabled = false;
        geoOptions.provider_routing = "off";
        check(PR::SourceDocument(geoOptions)["Geositeurl"] == sourceURL, "disabled and legacy off settings retain provider Geo sources");
        const auto geoError = PR::ResolveXrayGeo(PR::SourceDocument(geoOptions), geoConfig);
        check(geoError.isEmpty() && QJsonDocument(geoConfig).toJson().contains("ext:provider-geodata/") && !QJsonDocument(geoConfig).toJson().contains("geosite:WHITELIST"), "provider Geo rewrite covers routing values and DNS object keys");
        QJsonObject missing{{"routing", QJsonObject{{"rules", QJsonArray{QJsonObject{{"domain", QJsonArray{"geosite:MISSING"}}}}}}}};
        const auto beforeMissing = missing;
        check(PR::ResolveXrayGeo(PR::SourceDocument(geoOptions), missing).contains("MISSING") && missing == beforeMissing, "missing category reports a useful error without corrupting the config");
    }
    {
        QTableView table;
        QStandardItemModel model(2, ProfilesTableModel::ColumnCount);
        auto *header = new ProfilesTableFilterHeader(&table);
        table.setHorizontalHeader(header);
        table.setModel(&model);
        header->setSectionResizeMode(QHeaderView::Interactive);
        for (int i = 0; i < header->count(); ++i) header->resizeSection(i, 150 + i * 100);
        for (int width : {800, 1600, 1040, 800}) {
            table.resize(width, 300); table.show(); QApplication::processEvents();
            header->fitToWidth(table.viewport()->width()); QApplication::processEvents();
            check(header->length() == table.viewport()->width(), "profile columns fill the viewport on both growth and shrink");
            check(table.horizontalScrollBar()->maximum() == 0, "profile columns stay inside the viewport");
        }
        QVector<int> beforeDrag;
        for (int i = 0; i < header->count(); ++i) beforeDrag.append(header->sectionSize(i));
        const QPoint boundary(header->sectionViewportPosition(1) - 1, header->viewport()->height() - 5);
        QMouseEvent hover(QEvent::MouseMove, QPointF(boundary), QPointF(header->viewport()->mapToGlobal(boundary)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(header->viewport(), &hover);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(boundary), QPointF(header->viewport()->mapToGlobal(boundary)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(header->viewport(), &press);
        const auto moved = boundary + QPoint(20, 0);
        QMouseEvent drag(QEvent::MouseMove, QPointF(moved), QPointF(header->viewport()->mapToGlobal(moved)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(header->viewport(), &drag);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(moved), QPointF(header->viewport()->mapToGlobal(moved)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(header->viewport(), &release);
        check(header->sectionSize(0) > beforeDrag[0], "dragged profile column follows its boundary");
        check(header->sectionSize(0) + header->sectionSize(1) == beforeDrag[0] + beforeDrag[1], "drag only redistributes the adjacent column pair");
        for (int i = 2; i < header->count(); ++i) check(header->sectionSize(i) == beforeDrag[i], "drag preserves unrelated columns");
        check(header->length() == table.viewport()->width(), "drag keeps all columns inside the viewport");
        {
            QWidget host; host.resize(340, 250); host.show();
            RestartNotice notice(&host);
            notice.present("<p>Настройки изменены, перезапустите для применения</p><a href='restart'>Перезапустить</a> · <a href='ignore'>Игнорировать</a>");
            QApplication::processEvents();
            check(notice.verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff && host.rect().contains(notice.geometry()), "restart notice fits narrow window without scroll bars");
            host.resize(600, 300); QApplication::processEvents();
            check(host.rect().contains(notice.geometry()), "restart notice follows viewport resize");
        }
        {
            QTableView preview;
            QStandardItemModel rows(4,1);
            rows.setData(rows.index(0,0),1,ProfilesTableModel::TestStateRole);
            rows.setData(rows.index(1,0),2,ProfilesTableModel::TestStateRole);
            rows.setData(rows.index(1,0),"28 ms");
            rows.setData(rows.index(2,0),2,ProfilesTableModel::TestStateRole);
            rows.setData(rows.index(2,0),"RU",ProfilesTableModel::TestCountryRole);
            rows.setData(rows.index(2,0),QString::fromUtf8("28 ms 🇷🇺 ↓85.74 Mbps ↑103.23 Mbps"));
            rows.setData(rows.index(3,0),"39 ms");
            preview.setModel(&rows);preview.setItemDelegate(new TestResultDelegate(&preview));
            preview.resize(420,170);preview.horizontalHeader()->setStretchLastSection(true);preview.show();
            QApplication::processEvents();
            const auto image=preview.grab().toImage();
            check(!image.isNull(),"animated test indicators render inside table cells");
            const auto directory=qEnvironmentVariable("THRONE_SEASONAL_PREVIEW_DIR");
            if (!directory.isEmpty()) check(image.save(directory+"/test-indicators.png"),"test indicator preview saved");
        }
        DataViewHtmlGenerator panels;
        panels.seedLatencyTest(DataViewHtmlGenerator::LatencyTestPanelState::Kind::Url, 10);
        check(panels.buildHtml().isEmpty() && !panels.hasPriorityNotice(), "latency test has no top-bar indicator or decoration priority");
        panels.seedSpeedTest(3);
        panels.addTestProgress(1);
        check(panels.buildHtml().isEmpty() && !panels.hasPriorityNotice(), "speed test progress moves into table cells");
        panels.clearTestSections();
        panels.setDownloadReport({}, true);
        check(panels.hasPriorityNotice(), "update progress takes priority over decoration");
        panels.setDownloadReport({}, false);
        check(!panels.hasPriorityNotice(), "decoration returns after update progress clears");
        Configs::qwdtt rawPing;
        rawPing.server="2001:db8::1"; rawPing.server_port=56000; rawPing.rawPort=56003; rawPing.mode="raw";
        const auto probeConfig=QJsonDocument::fromJson(rawPing.NativePingConfig("{\"peer\":\"[2001:db8::1]:56003\",\"mode\":\"raw\"}").toUtf8()).object();
        check(probeConfig["peer"].toString()=="[2001:db8::1]:56000" && probeConfig["mode"].toString()=="wg" && probeConfig["probe_only"].toBool(),"RAW profile native ping uses the DTLS endpoint, including IPv6");
        check(rawPing.mode=="raw" && rawPing.rawPort==56003,"native ping does not change the connection mode");
        Configs::qwdtt q1, q2;
        Configs::csqtt c1, c2;
        check(!q1.deviceID.isEmpty() && q1.deviceID == q2.deviceID && q1.deviceID == c1.deviceID && c1.deviceID == c2.deviceID, "new relay profiles share a persistent client device identity");
        for (const auto &type : {QString("qwdtt"), QString("csqtt")}) {
            const QJsonObject old{{"_type", type}, {"tag", "Node"}, {"server", "old.example"}, {"server_port", 443}, {"password", "secret"}, {"device_id", "existing-device"}, {"hash_source", "auto"}, {"auto_hash_count", 3}};
            auto incoming = old; incoming.remove("_type"); incoming["server"] = "new.example"; incoming["device_id"] = "new-random-device";
            auto preserved = Subscription::PreserveRelaySettings(type, incoming, {old});
            check(preserved["device_id"] == "existing-device", "subscription server changes preserve relay device ID");
            incoming["tag"] = "Renamed node";
            preserved = Subscription::PreserveRelaySettings(type, incoming, {old});
            check(preserved["device_id"] == "existing-device", "complete relay replacement preserves the unique existing device ID");
        }
        for (const auto &type : {QString("qwdtt"), QString("csqtt")}) {
            const int limit = type == "csqtt" ? 6 : 4;
            QJsonArray previousSlots;
            for (int i = 1; i <= limit; ++i) previousSlots.append(QString("local-%1").arg(i));
            QJsonObject old{{"_type", type}, {"tag", "Hash slots"}, {"server", "example.net"}, {"server_port", 443}, {"password", "secret"}, {"device_id", "persisted"}, {"hash_source", "manual"}, {"hashes", previousSlots}};
            for (int count : {0, 1, 3, 4, limit}) {
                auto incoming = old;
                QJsonArray prefix;
                for (int i = 1; i <= count; ++i) prefix.append(QString("subscription-%1").arg(i));
                incoming["hashes"] = prefix;
                const auto merged = Subscription::PreserveRelaySettings(type, incoming, {old});
                auto expected = prefix;
                for (int i = count; i < previousSlots.size(); ++i) expected.append(previousSlots.at(i));
                check(merged["hashes"].toArray() == expected, "subscription owns leading hash slots and preserves trailing manual slots");
                old = merged; previousSlots = expected;
            }
            auto other = old; other["device_id"] = "another-device";
            auto incoming = old; incoming["hashes"] = QJsonArray{"new"};
            check(Subscription::PreserveRelaySettings(type, incoming, {old, other})["hashes"] == incoming["hashes"], "ambiguous relay profiles never copy manual hashes");
        }
        for (const auto &type : {QString("qwdtt"), QString("csqtt")}) {
            QJsonObject old{{"_type",type},{"tag","Workers"},{"hash_source","manual"},{"hashes",QJsonArray{"one"}},{"workers",27}};
            auto incoming=old; incoming["workers"]=9; incoming["hashes"]=QJsonArray{"one","two","three"};
            auto merged=Subscription::PreserveRelaySettings(type,incoming,{old});
            check(merged["workers"].toInt()==(type=="csqtt" ? 20 : 27),"subscription never increases the chosen worker count");
            old=merged; old["workers"]=108; incoming["hashes"]=QJsonArray{"same","same","same","same"};
            merged=Subscription::PreserveRelaySettings(type,incoming,{old});
            check(merged["workers"].toInt()==(type=="csqtt" ? 100 : 27),"workers clamp only to the final protocol limit");
        }
        for (const auto &type : {QString("qwdtt"), QString("csqtt")}) {
            // The user's own choices survive a subscription refresh: hashes, workers, mode and TURN transport.
            const QJsonObject old{{"_type", type}, {"tag", "Choices"}, {"server", "example.org"}, {"server_port", 443}, {"password", "secret"},
                                  {"hash_source", "manual"}, {"hashes", QJsonArray{"user-hash-1", "user-hash-2"}}, {"workers", 18},
                                  {"mode", "raw"}, {"turn_tcp", type == "qwdtt" ? false : true}};
            const QJsonObject incoming{{"tag", "Choices"}, {"server", "example.org"}, {"server_port", 443}, {"password", "secret"},
                                       {"hashes", QJsonArray{}}, {"workers", 9}, {"mode", "wg"}, {"turn_tcp", type == "qwdtt"}};
            const auto merged = Subscription::PreserveRelaySettings(type, incoming, {old});
            check(merged["hashes"] == old["hashes"] && merged["workers"].toInt() == (type == "csqtt" ? 10 : 18), "subscription refresh keeps the user's hashes and workers");
            check(merged["turn_tcp"] == old["turn_tcp"], "subscription refresh keeps the user's TURN transport");
            if (type == "qwdtt") check(merged["mode"].toString() == "raw", "subscription refresh keeps the user's qWDTT mode");
            auto automatic = old; automatic["hash_source"] = "auto"; automatic["auto_hash_count"] = 2; automatic["workers"] = 54;
            const auto autoMerged = Subscription::PreserveRelaySettings(type, incoming, {automatic});
            check(autoMerged["hash_source"].toString() == "auto" && autoMerged["auto_hash_count"].toInt() == 2 && autoMerged["workers"].toInt() == (type == "csqtt" ? 50 : 54),
                  "subscription refresh keeps automatic hashes from the VK binding and their workers");
        }
        for (const auto &type : {QString("qwdtt"), QString("csqtt")}) {
            // The subscription gave one hash and the user added three: a refresh keeps all four.
            const QJsonObject user{{"_type", type}, {"tag", "Slots"}, {"server", "example.org"}, {"server_port", 443}, {"password", "secret"},
                                   {"hash_source", "manual"}, {"hashes", QJsonArray{"sub-1", "mine-2", "mine-3", "mine-4"}}};
            QJsonObject refresh{{"tag", "Slots"}, {"server", "example.org"}, {"server_port", 443}, {"password", "secret"}, {"hashes", QJsonArray{"sub-1"}}};
            check(Subscription::PreserveRelaySettings(type, refresh, {user}).value("hashes").toArray() == QJsonArray{"sub-1", "mine-2", "mine-3", "mine-4"},
                  "a refresh keeps the hashes the user added after the subscription's");
            // The subscription now gives two hashes: they take the first two slots, the user's later ones stay.
            refresh["hashes"] = QJsonArray{"sub-1-new", "sub-2-new"};
            check(Subscription::PreserveRelaySettings(type, refresh, {user}).value("hashes").toArray() == QJsonArray{"sub-1-new", "sub-2-new", "mine-3", "mine-4"},
                  "subscription hashes replace their slots and the user's hashes after them stay");
        }
        const auto hy = parse(R"({"remarks":"HY2","outbounds":[{"protocol":"freedom","tag":"direct"},{"protocol":"hysteria","tag":"hy","settings":{"address":"hy.example","port":443}}],"routing":{"balancers":[{"tag":"auto","selector":["hy"]}],"rules":[{"balancerTag":"auto"}]}})");
        check(hy.size() == 1 && hy.first()->outbound->DisplayType() == "Hysteria" && hy.first()->outbound->DisplayAddress() == "hy.example:443", "JSON Hysteria2 retains its protocol and address despite embedded balancers");
        for (const auto &protocol : QStringList{"vless", "vmess", "trojan", "shadowsocks", "socks", "http", "wireguard"}) {
            Configs::Custom custom;
            custom.type = Configs::Custom::CustomXrayFullConfig;
            custom.config = QString::fromUtf8(QJsonDocument(QJsonObject{{"outbounds", QJsonArray{QJsonObject{{"protocol", protocol}, {"tag", "proxy"}, {"settings", QJsonObject{{"address", "protocol.example"}, {"port", 443}}}}}}, {"routing", QJsonObject{{"balancers", QJsonArray{QJsonObject{{"tag", "auto"}, {"selector", QJsonArray{"proxy"}}}}}, {"rules", QJsonArray{QJsonObject{{"balancerTag", "auto"}}}}}}}).toJson());
            check(custom.DisplayType().contains(protocol, Qt::CaseInsensitive) && custom.DisplayAddress() == "protocol.example:443", "Xray JSON protocol display is generic across protocols");
        }
        Configs::Custom singBox;
        singBox.type = Configs::Custom::CustomFullConfig;
        singBox.config = R"({"outbounds":[{"type":"direct","tag":"direct"},{"type":"selector","tag":"auto","outbounds":["hy"]},{"type":"hysteria2","tag":"hy","server":"native.example","server_port":443}],"route":{"final":"auto"}})";
        check(singBox.DisplayType() == "Hysteria" && singBox.DisplayAddress() == "native.example:443", "sing-box JSON resolves selected proxy protocol and address");
    }
    {
        for (const auto &protocol : QStringList{"vless", "vmess", "trojan", "shadowsocks", "socks", "http", "wireguard", "hysteria", "hysteria2"}) {
            const QJsonObject node{{"protocol", protocol}, {"tag", "Node"}, {"settings", QJsonObject{{"address", "json.example"}, {"port", 443}, {"id", "00000000-0000-4000-8000-000000000001"}}}};
            const auto bytes = QJsonDocument(node).toJson(QJsonDocument::Compact);
            const auto plain = parse(bytes);
            for (const auto &prefix : {QByteArray("json://#"), QByteArray("throne://add/")}) {
                const auto encoded = parse(prefix + bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
                check(plain.size() == 1 && encoded.size() == 1 && plain.first()->outbound->ExportToJson() == encoded.first()->outbound->ExportToJson(), "every Xray protocol uses identical plain and encoded JSON import");
            }
            if (!plain.isEmpty()) {
                const auto label = plain.first()->outbound->DisplayType();
                check(protocol == "vless" ? label == "VLESS (Xray)" : !label.contains("Xray"), "only the native VLESS engine label is appended");
                if (protocol.startsWith("hysteria")) check(label == "Hysteria", "original Hysteria label restored");
            }
        }
        for (const auto &type : QStringList{"socks", "http", "shadowsocks", "vmess", "vless", "xrayvless", "trojan", "hysteria", "tuic", "anytls", "wireguard", "masque", "tailscale", "ssh", "naive", "mieru", "snell", "shadowtls", "juicity", "trusttunnel", "openvpn", "openconnect", "chain", "autoselector", "extracore", "custom"}) {
            const auto original = Configs::ProfilesRepo::NewProfile(type);
            if (type == "extracore") original->outbound->ParseFromJson(QJsonObject{{"socks_port", 1080}});
            const auto bytes = QJsonDocument(original->outbound->ExportToJson()).toJson(QJsonDocument::Compact);
            const auto plain = parse(bytes);
            const auto encoded = parse(original->outbound->ExportJsonLink().toUtf8());
            check(plain.size() == encoded.size() && !plain.isEmpty(), "registered native exports import from JSON links");
            if (!plain.isEmpty() && !encoded.isEmpty()) check(plain.first()->outbound->ExportToJson() == encoded.first()->outbound->ExportToJson(), "native export settings retained identically");
        }
        const QJsonObject relay{{"type", "qwdtt"}, {"peer", "203.0.113.8:56000"}, {"password", "secret"}, {"device_id", "persisted"}};
        const QJsonObject cs{{"type", "csqtt"}, {"server", "203.0.113.9"}, {"server_port", 46000}, {"password", "secret"}, {"device_id", "cs-persisted"}};
        const QJsonObject hy{{"type", "hysteria2"}, {"server", "hy.example"}, {"server_port", 443}, {"password", "secret"}};
        const QJsonObject full{{"remarks", "Full"}, {"outbounds", QJsonArray{QJsonObject{{"protocol", "hysteria"}, {"tag", "hy"}, {"settings", QJsonObject{{"address", "full.example"}, {"port", 443}}}}}}, {"routing", QJsonObject{{"rules", QJsonArray{QJsonObject{{"outboundTag", "hy"}}}}}}};
        const QJsonArray nodes{relay, hy, cs, full};
        for (const auto &document : {QJsonDocument(nodes), QJsonDocument(QJsonObject{{"profiles", nodes}}), QJsonDocument(QJsonObject{{"servers", nodes}})}) {
            const auto bytes = document.toJson(QJsonDocument::Compact);
            for (const auto &body : {bytes, QByteArray("throne://add/") + bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)}) {
                const auto imported = parse(body);
                check(imported.size() == 4, "mixed qWDTT CSQTT HY2 and full Xray documents preserve every entry");
                if (imported.size() == 4) check(imported[0]->type == "qwdtt" && imported[1]->outbound->DisplayType() == "Hysteria" && imported[2]->type == "csqtt" && imported[3]->Custom() && imported[3]->Custom()->IsXrayFullConfig(), "mixed array order and full routing retained");
            }
        }
    }

    {
        const auto wrapped = QStringLiteral("https://ssconnect.app/?q=happ%3A%2F%2Fadd%2Fhttps%3A%2F%2Flink.flagman.click%2Fsub%2Fexample");
        check(Subscription::UnwrapProviderLink(wrapped) == "https://link.flagman.click/sub/example", "SSConnect unwraps without fetching its HTML");
        check(Subscription::UnwrapProviderLink(wrapped + "&q=second").isEmpty(), "duplicate wrapper payload rejected");
        check(Subscription::UnwrapProviderLink("https://ssconnect.app:444/?q=secret").isEmpty(), "nonstandard wrapper port rejected");
        const QByteArray masqueLink = "masque://MHcCAQEEKey%2BWith%2FSlash@162.159.198.2:443?publickey=Peer%2BPublic%2FKey&address=172.16.0.2%2C2001%3Adb8%3A%3A1&network=h2&sni=4pda.to#MASQUE%20h2";
        const auto imported = parse(masqueLink);
        check(imported.size() == 1, "MASQUE clipboard URI imports");
        if (imported.size() == 1) {
            const auto *m = imported.first()->Masque();
            check(m && m->private_key == "MHcCAQEEKey+With/Slash" && m->peer_public_key == "Peer+Public/Key" &&
                m->http_version == 2 && m->tls->server_name == "4pda.to" && m->address == QStringList{"172.16.0.2/32","2001:db8::1/128"}, "MASQUE preserves escaped keys, addresses, HTTP/2 and SNI");
            Configs::masque roundtrip;
            check(roundtrip.ParseFromLink(imported.first()->outbound->ExportToLink()) && roundtrip.ExportToJson() == imported.first()->outbound->ExportToJson(), "MASQUE URI round trip");
        }
        const auto yaml = parse(R"yaml(warp: &warp
  type: wireguard
  ip: 172.16.0.2
  ipv6: 2001:db8::1
  private-key: AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=
  public-key: BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB=
awg: &awg
  amnezia-wg-option:
    jc: 4
    jmin: 40
    jmax: 70
    i1: <b 0x01020304>
    content-padding-addition: 30-84
    random-trailers: true
msq: &msq
  type: masque
  private-key: MASQUEPrivateKey
  public-key: MASQUEPublicKey
  ip: 172.16.0.2
  ipv6: 2001:db8::2
proxies:
- <<: [*warp, *awg]
  name: AWG example
  server: 192.0.2.1
  port: 500
- <<: *msq
  name: MASQUE
  server: 192.0.2.2
  port: 443
  sni: 4pda.to
- <<: *msq
  name: MASQUE h2
  server: 192.0.2.2
  port: 443
  network: h2
  sni: 4pda.to
)yaml");
        check(yaml.size() == 3, "Clash file and clipboard import resolve mapping and sequence merge aliases");
        if (yaml.size() == 3) {
            const auto *wg = yaml[0]->Wireguard();
            check(wg && wg->enable_amnezia && wg->jc == 4 && wg->i1 == "<b 0x01020304>" &&
                wg->content_padding_addition == "30-84" && wg->random_trailers && wg->peer->port == 500, "Clash WireGuard and AWG3 settings preserved");
            check(yaml[1]->Masque() && yaml[1]->Masque()->http_version == 3 && yaml[1]->Masque()->disable_version_fallback &&
                yaml[2]->Masque() && yaml[2]->Masque()->http_version == 2 && yaml[2]->Masque()->tls->server_name == "4pda.to", "Clash MASQUE templates retain HTTP version and SNI");
        }
        Configs::masque invalid;
        check(!invalid.ParseFromLink("masque://key@192.0.2.1?address=not-ip") &&
            !invalid.ParseFromLink("masque://key@192.0.2.1?address=172.16.0.2&address=::1"), "invalid and duplicate MASQUE addresses rejected");
    }
    {
        for (const auto &type : QStringList{"qwdtt","csqtt","wdttplus"}) {
            for (const auto &source : QStringList{"manual","auto","auto_js"}) {
                if (source == "auto_js" && type != "csqtt") continue;
                auto profile = Configs::ProfilesRepo::NewProfile(type);
                QJsonObject saved{{"type",type},{"_type",type},{"tag","Persistent choices"},{"server","192.0.2.1"},{"server_port",46000},
                    {"password","fixture"},{"device_id","persistent-device"},{"hash_source",source},{"auto_hash_count",2},
                    {"manual_hashes_override",true},{"hashes",QJsonArray{"user_first_hash_123456","user_second_hash_123456"}},
                    {"workers",type=="csqtt" ? 50 : 27},{"mode","raw"},{"turn_tcp",false},{"turn_transport","tcp_tls"},
                    {"srtp_mode","dtls"},{"tcp_only",true},{"turn_sni","turn.example.org"},{"rt_masque",true},
                    {"rt_masque_server_bootstrap",true},{"captcha_mode","wv"}};
                check(profile->outbound->ParseFromJson(saved), "saved relay choices load");
                for (int refresh = 0; refresh < 3; ++refresh) {
                    saved = profile->outbound->ExportToJson(); saved["_type"] = type;
                    QJsonObject incoming{{"type",type},{"tag","Persistent choices"},{"server",QString("192.0.2.%1").arg(refresh+10)},
                        {"server_port",46000},{"password","fixture"},{"hashes",QJsonArray{"provider_replaced_hash_123456"}},
                        {"hash_source","manual"},{"workers",type=="csqtt" ? 100 : 108}};
                    const auto merged = Subscription::PreserveRelaySettings(type, incoming, {saved});
                    check(merged["workers"] == saved["workers"] && merged["hash_source"] == saved["hash_source"] && merged["device_id"] == saved["device_id"], "repeated refresh preserves workers, hash source and device");
                    if (source == "manual") check(merged["hashes"] == saved["hashes"], "even the first manually edited hash survives provider replacement");
                    for (const auto *key : {"mode","turn_tcp","turn_transport","srtp_mode","tcp_only","turn_sni","rt_masque","rt_masque_server_bootstrap","captcha_mode"})
                        if (saved.contains(key)) check(merged[key] == saved[key], "manual transport and captcha choices survive refresh");
                    check(profile->outbound->ParseFromJson(merged), "preserved choices survive serialization and restart");
                }
            }
        }
    }
    const QByteArray csqttLink = "csqtt://connect?v=2&host=203.0.113.7&peer=46000&password=p%40ss&hashes=abcdefghijklmno%2Bp+abcdefghijklmnop2";
    const auto csProfiles = parse(csqttLink);
    check(csProfiles.size() == 1, "CSQTT subscription link imports one profile");
    {
        Configs::csqtt tokenLink;
        check(tokenLink.ParseFromLink("csqtt://connect?v=2&host=192.0.2.1&peer=46000&password=fixture&token=vk1.private%2Btoken"), "CSQTT Legacy own-token link imports");
        check(tokenLink.hashSource == "auto_js" && tokenLink.vkAnonPath == "auto_js" && tokenLink.vkAccessToken == "vk1.private+token", "link token selects original Auto JS and preserves plus");
        const auto encrypted = tokenLink.ExportToLink();
        const auto html = QByteArray("<!doctype html><html><input value=\"") + encrypted.toUtf8() + "\"><a href=\"" + encrypted.toUtf8() + "\">Connect</a></html>";
        const auto subscription = parse(html);
        check(!encrypted.isEmpty() && subscription.size() == 1, "Legacy QR HTML subscription imports its encrypted profile once");
        if (subscription.size() == 1) {
            const auto *cs = dynamic_cast<Configs::csqtt *>(subscription.first()->outbound.get());
            check(cs && cs->password == tokenLink.password && cs->vkAccessToken == tokenLink.vkAccessToken, "Legacy subscription credentials survive decode");
        }
        auto automatic = tokenLink.ExportToJson(); automatic["hash_source"] = "auto";
        check(tokenLink.ParseFromJson(automatic) && tokenLink.vkAnonPath == "vkcalls", "Auto API overrides a stale Auto JS authorization mode");
    }
    if (csProfiles.size() == 1) {
        auto *cs = dynamic_cast<Configs::csqtt *>(csProfiles.first()->outbound.get());
        check(cs && cs->hashes.size() == 2 && cs->hashes.first() == "abcdefghijklmno+p", "CSQTT hash plus survives URL decoding");
        if (cs) {
            check(cs->DisplayType() == "CSQTT (UDP)", "CSQTT default transport label");
            cs->turnTCP = true; cs->turnTransport = "tcp";
            check(cs->DisplayType() == "CSQTT (TCP)", "CSQTT TCP transport label");
            Configs::csqtt roundtrip;
            cs->name = QString::fromUtf8("РўРµСЃС‚ + # CSQTT");
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
        check(csWorkers && csWorkers->count() == 11 && csWorkers->itemData(10).toInt() == 100, "CSQTT Legacy selector offers 5 and 10..100 workers independent of hashes");
    }
    check(parse("csqtt://connect?v=3&host=203.0.113.7&peer=46000&password=secret").isEmpty(), "unknown CSQTT link version rejected");
    const auto excessCSQTT = parse("csqtt://connect?v=2&host=203.0.113.7&peer=46000&password=secret&hashes=abcdefghijklmnop1+abcdefghijklmnop2+abcdefghijklmnop3+abcdefghijklmnop4+abcdefghijklmnop5+abcdefghijklmnop6+ignored");
    check(excessCSQTT.size() == 1, "excess CSQTT hashes do not prevent import");
    if (excessCSQTT.size() == 1) {
        auto *cs = dynamic_cast<Configs::csqtt *>(excessCSQTT.first()->outbound.get());
        check(cs && cs->hashes.size() == 6 && cs->hashes.first() == "abcdefghijklmnop1" && cs->hashes.last() == "abcdefghijklmnop6", "CSQTT retains first six hashes in order");
    }
    {
        // A pasted VK call link is read as its hash, as stripVkUrlStatic of both original apps does.
        const QStringList pasted{"https://vk.com/call/join/AbCdEfGhIjKlMnOp1?from=chat", "vk.ru/call/join/AbCdEfGhIjKlMnOp2/",
                                 "M.VK.COM/call/join/AbCdEfGhIjKlMnOp3#x", "http://m.vk.ru/call/join/AbCdEfGhIjKlMnOp4", "AbCdEfGhIjKlMnOp5"};
        const QStringList expected{"AbCdEfGhIjKlMnOp1", "AbCdEfGhIjKlMnOp2", "AbCdEfGhIjKlMnOp3", "AbCdEfGhIjKlMnOp4", "AbCdEfGhIjKlMnOp5"};
        check(Configs::qwdtt::NormalizeHashes(pasted) == expected, "qWDTT reads pasted VK call links as hashes");
        check(Configs::csqtt::NormalizeHashes(pasted) == expected, "CSQTT reads pasted VK call links as hashes");
    }
    {
        // WDTT Plus links, as its TransferCodec builds them, and plain links of the original WDTT.
        const QString hash = "abcDEF123_-xyz789";
        const auto plus = parse(("wdtt://connect?v=1&host=192.0.2.5&dtls=56000&wg=56001&local=9000&password=p%40ss&hashes=" + hash + "&name=Home&max_workers=36").toUtf8());
        check(plus.size() == 1 && plus.first()->type == "wdttplus", "wdtt://connect imports a WDTT Plus profile");
        if (plus.size() == 1) {
            auto *o = dynamic_cast<Configs::wdttplus *>(plus.first()->outbound.get());
            check(o && o->DisplayType() == "WDTT Plus" && o->server == "192.0.2.5" && o->server_port == 56000 && o->wgPort == 56001
                  && o->localPort == 9000 && o->password == "p@ss" && o->hashes == QStringList{hash} && o->name == "Home"
                  && o->maxWorkers == 36 && o->workers == 36, "WDTT Plus link fields");
            if (o) {
                check(!o->ExportToLink().contains("device") && !o->ExportToLink().contains(o->deviceID), "the device id never travels in a WDTT Plus link");
                Configs::wdttplus again;
                auto a = o->ExportToJson(), b = QJsonObject();
                check(again.ParseFromLink(o->ExportToLink()), "WDTT Plus link round trip parses");
                b = again.ExportToJson(); a.remove("device_id"); b.remove("device_id");
                check(a == b, "WDTT Plus link round trip keeps every field");
                EditWdttPlus editor;
                editor.onStart(plus.first());
                auto *workers = editor.findChild<QComboBox *>("wdttPlusWorkers");
                check(workers && workers->count() == 4 && workers->itemData(3).toInt() == 36, "the server's max_workers caps the WDTT Plus worker choice");
            }
        }
        const auto legacy = parse(("WDTT://192.0.2.6:56000:56001:9000:secret:" + hash).toUtf8());
        check(legacy.size() == 1 && legacy.first()->type == "qwdtt", "a plain wdtt:// link opens as qWDTT, whatever the scheme's case");
        if (legacy.size() == 1) {
            auto *o = dynamic_cast<Configs::qwdtt *>(legacy.first()->outbound.get());
            check(o && o->server == "192.0.2.6" && o->server_port == 56000 && o->password == "secret" && o->hashes == QStringList{hash} && o->mode == "wg",
                  "plain WDTT link fields become a qWDTT profile in WG mode");
        }
        Configs::qwdtt notPlus;
        check(!notPlus.ParseFromLink("wdtt://connect?v=1&host=192.0.2.5&dtls=56000&wg=56001&local=9000&password=p"), "qWDTT leaves wdtt://connect to WDTT Plus");
        {
            const QJsonObject stored{{"type", "wdttplus"}, {"legacy", true}, {"tag", "Home"}, {"server", "192.0.2.6"}, {"server_port", 56000},
                {"wg_port", 56001}, {"local_port", 9000}, {"password", "secret"}, {"hashes", QJsonArray{hash}}, {"workers", 18}};
            const auto restored = Configs::wdttplus::LegacyToQwdtt(stored);
            check(restored["type"] == "qwdtt" && restored["tag"] == "Home" && restored["server"] == "192.0.2.6" && restored["mode"] == "wg"
                  && restored["password"] == "secret", "a WDTT profile stored by 1.5 becomes qWDTT again");
            auto plus = stored;
            plus.remove("legacy");
            check(Configs::wdttplus::LegacyToQwdtt(plus).isEmpty(), "real WDTT Plus profiles stay WDTT Plus");
        }
        for (const auto *bad : {"wdtt://connect?v=2&host=192.0.2.5&dtls=56000&wg=56001&local=9000&password=p",
                                "wdtt://connect?v=1&host=192.0.2.5&dtls=56000&wg=56001&local=9000&password=p&max_workers=40",
                                "wdtt://connect?v=1&host=&dtls=56000&wg=56001&local=9000&password=p",
                                "wdtt://192.0.2.6:56000:9000:secret"}) {
            Configs::wdttplus rejected;
            check(!rejected.ParseFromLink(bad), "malformed WDTT Plus links are rejected");
        }
        check(Configs::wdttplus::NormalizeWorkers(20, 0) == 18 && Configs::wdttplus::NormalizeWorkers(500, 0) == 108
              && Configs::wdttplus::NormalizeWorkers(1, 0) == 9 && Configs::wdttplus::NormalizeWorkers(108, 45) == 45,
              "WDTT Plus workers go in groups of 9 up to 108 or the server's limit");
        Configs::wdttplus fresh;
        Configs::qwdtt sibling;
        check(fresh.workers == 18 && fresh.server_port == 56000 && fresh.wgPort == 56001 && fresh.localPort == 9000 && fresh.deviceID == sibling.deviceID,
              "new WDTT Plus profiles follow the app's defaults and this computer's device id");
        check(EditWdttPlus::HashVerdict("ok") == QStringLiteral("TURN доступен") && EditWdttPlus::HashVerdict("Weird").contains(QStringLiteral("Диагностика")),
              "WDTT Plus hash verdicts are shown as fixed texts");
        // A refresh keeps the user's hashes after the subscription's, and their workers within the server's limit.
        const QJsonObject user{{"_type", "wdttplus"}, {"tag", "Plus"}, {"server", "example.org"}, {"server_port", 56000}, {"password", "secret"},
                               {"hash_source", "manual"}, {"hashes", QJsonArray{"sub-1", "mine-2"}}, {"workers", 54}, {"device_id", "kept"}};
        const QJsonObject refresh{{"tag", "Plus"}, {"server", "example.org"}, {"server_port", 56000}, {"password", "secret"}, {"hashes", QJsonArray{"sub-1-new"}}, {"workers", 18}};
        const auto merged = Subscription::PreserveRelaySettings("wdttplus", refresh, {user});
        check(merged.value("hashes").toArray() == QJsonArray{"sub-1-new", "mine-2"} && merged["workers"].toInt() == 54 && merged["device_id"] == "kept",
              "a WDTT Plus refresh keeps the user's hashes, workers and device");
        // A file of links and a QR picture reach the same parser: WDTT and WDTT Plus are recognised both ways.
        const auto file = parse(("wdtt://192.0.2.6:56000:56001:9000:secret:" + hash + "\ncsqtt://connect?v=2&host=203.0.113.7&peer=46000&password=s").toUtf8());
        check(file.size() == 2 && file[0]->type == "qwdtt" && file[1]->type == "csqtt", "a file with a WDTT link and a CSQTT link imports both");
        const auto shared = "wdtt://connect?v=1&host=192.0.2.5&dtls=56000&wg=56001&local=9000&password=p&hashes=" + hash + "&name=QR";
        const auto decoded = QrDecoder().decode(ShareBlocks::QrImage(shared).convertToFormat(QImage::Format_Grayscale8));
        check(decoded.size() == 1 && decoded.first() == shared, "the QR code of a WDTT Plus link decodes back to the link");
        if (decoded.size() == 1) {
            const auto fromQr = parse(decoded.first().toUtf8());
            check(fromQr.size() == 1 && fromQr.first()->type == "wdttplus" && fromQr.first()->outbound->name == "QR", "a WDTT Plus QR picture imports as WDTT Plus");
        }
        auto limited = refresh; limited["max_workers"] = 27;
        check(Subscription::PreserveRelaySettings("wdttplus", limited, {user})["workers"].toInt() == 27, "a WDTT Plus refresh respects a new server limit");
    }
    Configs::qwdtt defaults;
    check(defaults.mode == "wg" && defaults.turnTCP, "new profiles default to the original app's WG over TURN TCP");
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
        auto qwdttOriginal = testProfiles.first();
        const auto qwdttDevice = dynamic_cast<Configs::qwdtt *>(qwdttOriginal->outbound.get())->deviceID;
        auto qwdttChanged = qwdttOriginal->outbound->ExportToJson();
        qwdttChanged.remove("device_id"); qwdttChanged["tag"] = "Renamed qWDTT subscription";
        qwdttChanged["hashes"] = QJsonArray{"abcdefghijklmnop-new-call"};
        auto qwdttRefreshed = Configs::ProfilesRepo::NewProfile("qwdtt");
        check(qwdttRefreshed->outbound->ParseFromJson(qwdttChanged), "updated qWDTT subscription parses");
        dynamic_cast<Configs::qwdtt *>(qwdttRefreshed->outbound.get())->deviceID = "new-random-device";
        Configs::dataManager->profilesRepo->RestoreTransportIdentity(qwdttRefreshed.get());
        check(dynamic_cast<Configs::qwdtt *>(qwdttRefreshed->outbound.get())->deviceID == qwdttDevice, "qWDTT database binding survives profile reconstruction");
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
    {
        // Links from OpenFlux's share package: frozen ones from share/compat_test.go and ones share.Make writes today.
        const auto read = [](const QString &link) {
            auto profile = std::make_shared<Configs::openflux>();
            return profile->ParseFromLink(link) ? profile : nullptr;
        };
        const auto types = [](const std::shared_ptr<Configs::openflux> &profile) {
            QStringList list;
            for (const auto &entry : profile->transports) list << entry.toObject()["type"].toString();
            return list;
        };
        const QString cupsLink = "openflux://v1/TMpBCsIwEAXQu_y1aTCJm4B4EHEhNehAnAmTaUVK7y6ii771W2B65d5ErSOfF9i7FWSMU-vClbhgh0krMh5mrWfvia3oTOU1fNPwW77SXNwoN-K7P6nI87j_cyGE4GKM0aWUkjtsYL2snwAAAP__";
        const auto cups = read(cupsLink);
        check(cups && types(cups) == QStringList{"cupsonline"} && !cups->negotiate, "OpenFlux reads frozen classic Cups.online link");
        check(cups && cups->context.isEmpty() && !cups->ExportToJson().contains("context"), "OpenFlux never invents an encryption context");
        const auto mailru = read("openflux://v1/qlbKS8xNVbJS8k3MzNErKlVIK8pMzUtR0lEqKUrMKy7ILyopVrKKrlYqqSwAKctNzMwpKlXSUSotylGyUnJMck5xTXPPMNT3zPLO8c3zLzBSqo2tBQQAAP__");
        check(mailru && mailru->name == "Mail.ru friend" && mailru->transports.first().toObject()["url"].toString() == "AbCdEfGh1/IjKlMnOp2", "OpenFlux reads frozen Mail.ru link");
        const QString sessionLink = "openflux://v1/rI_BaiwhEEV_pSnonWPb7TweCCFkM-QfQha2FhlJj0pZTkaG_vdgAoHssyw4dc-9d4j2gmDgOV0QBER8SxwsIximigIKOkIGAy4RoePhnKjgsFpmpDYUtnnrjy5FxlsHz8y5mGnyyRXZbPR4k1S_zuka8OOx0vbQ7MGH8n7Idd2CG_XTuJzG5WRXBwKYbCw5ERcwL3fglnvD63cWCKi0_YknU0gUuIGZldrFj8mHvvQX8E8J8MF276K0VHKetfxvjketYX_dPwMAAP__";
        const auto session = read(sessionLink);
        check(session && session->negotiate && session->secret == "correct horse battery staple" && session->server == "203.0.113.7" && session->server_port == 4433, "OpenFlux reads frozen Session link");
        const auto legacy = read("openflux://v1/JMk7DsIwDAbgu_xz1PB--CqIIdgGhqqJbFdqVPXuDMzfCq6iDMKon8IdCa5sGiDs9ofj6Xy53u7lxaJvJISVyVu1cNBjRfSmIPQyiS5ImG0E4RvRnHKWyj78bbA5L9ie2y8AAP__");
        check(legacy && legacy->codec == "legacy", "OpenFlux keeps the legacy codec");
        const auto node = read("openflux://v1/jM7BSsQwFIXhVwlnHZJ0OiJmJz6GuLgmVxvQJNzcypSh7y5lQHA328PPx7mi0jcjorbMsKj82bSQMqLKyhaDk7AigsxYSDib22Lah5lPJi0klJRlwCK1qnw54kW1j-h9bmm4jWrmi5PVcy7qs39-f4GFCtXRm-hAfL1Ct378-LnVsFjl606pS2lSdEOcQtjtn5WLcNJ_wUOwyIUO-RRmF9w0ze4xPp3PM_a3_XcA");
        check(node && types(node) == QStringList{"vyandex", "direct"} && node->context == "https://docs.yandex.ru/edit/d/AbC", "OpenFlux reads node wizard link with Volga document and direct backup");
        const auto telemost = read("openflux://v1/lI5NSsAwFISvEmYdGm39zVXExSN9WqVNyssrtJQu9CjeoSvFM6Q3kiIewN0wzHx8KyINDI_yUfbyWfbyVb6Pt-MdFpGfk76QMrzKxBaZg7DCg0zuSLg1v41JT6apTehIKChLhkVIUXk-x53qmL1zyj0PKWu1UGx5rmRyr-6ybq6ub27v7i_OBAsVinlMohn-YYUu42n394XFJP0_odvj9jMA");
        check(telemost && telemost->name == QStringLiteral("Телемост") && types(telemost) == QStringList{"telemost"}, "OpenFlux reads Telemost link");
        const auto avito = read("openflux://v1/TMwxCgJBDIXhq4RXj412exWxiDGygm6WlyjKMneXwcb25-PfkG70wgSVnJV-kV-RuMphLzYr1cqZaLBYyt8DM-Kx07OhoahLrsFKTMcN9Vl93F63CjQ8ef_n_dS_AwA");
        check(avito && types(avito) == QStringList{"avito"}, "OpenFlux reads Avito link");
        const auto stream = read("openflux://v1/JMnBCsMgDAbgd_nPstzzKmMH2YQV1IQkQov47kV6_iaa_AoYHlZyQ0JY7q5i4eD3RFy6-TvUpdejFyQMq2D8I9SZaNPrMTKRRifWZ90DAA");
        check(stream && stream->mode == "stream", "OpenFlux reads phpbox stream link");
        const auto body = sessionLink.mid(QStringLiteral("openflux://v1/").size());
        auto swapped = body; swapped.replace('-', '+').replace('_', '/');
        QStringList mangledLinks;
        mangledLinks << QStringLiteral("  ") + sessionLink + QStringLiteral("\n")
                     << sessionLink + QString((4 - body.size() % 4) % 4, QLatin1Char('='))
                     << QStringLiteral("openflux://v1/") + swapped
                     << QStringLiteral("openflux://v1/") + body.left(30) + QString(QChar(0x200b)) + body.mid(30);
        for (const auto &mangled : mangledLinks) {
            check(read(mangled) != nullptr, "OpenFlux reads a link mangled by a chat");
        }
        check(parse(("\n" + sessionLink + "\n").toUtf8()).size() == 1, "OpenFlux link imports from the clipboard");
        check(read(cupsLink.toUpper()) == nullptr && read("openflux://v2/abc") == nullptr && read(cupsLink.chopped(30)) == nullptr, "OpenFlux rejects changed or truncated links");
        Configs::openflux stored;
        check(stored.ParseFromJson(QJsonObject{{"type", "openflux"}, {"negotiate", true}, {"secret", "a shared secret of 32 characters"}, {"context", "http://openflux"},
                                               {"transports", QJsonArray{QJsonObject{{"type", "yandex"}, {"url", "https://disk.yandex.ru/i/abc"}}}}})
              && stored.context.isEmpty(), "OpenFlux drops the context earlier builds invented");
        if (node) {
            const auto again = read(node->ExportToLink());
            check(again && again->ExportIdentity() == node->ExportIdentity() && again->name == node->name, "OpenFlux exported link reads back");
            check(node->Prepare().isEmpty(), "OpenFlux node profile prepares its helper");
            const auto prepared = QJsonDocument::fromJson(node->extraCoreConf.toUtf8()).object();
            const auto user = prepared["socks_user"].toString();
            check(!user.isEmpty() && prepared["socks_pass"].toString() == user && node->Build().object["username"].toString() == user,
                  "OpenFlux helper listener requires per-launch credentials");
        }
    }
    {
        // Links of the dnst:// specification (net2share/dnst-url-spec EXAMPLES.md) and of the apps that predate it.
        const QString key = "a5d214d305ad581b00b53d1887f99d8d3093ff5a680762c6ca8e81aaaa69ad08";
        const auto read = [&parse](const QString &link) -> std::shared_ptr<Configs::Profile> {
            const auto profiles = parse(link.toUtf8());
            return profiles.size() == 1 ? profiles.first() : nullptr;
        };
        const auto tunnel = [](const std::shared_ptr<Configs::Profile> &profile) {
            return profile ? dynamic_cast<Configs::dnstunnel *>(profile->outbound.get()) : nullptr;
        };
        const auto dnstt = read("dnst://t.example.com/dnstt/ssh?pubkey=" + key + "&user=tunnel&password=secret#my-tunnel");
        check(dnstt && dnstt->type == "dnstt" && tunnel(dnstt)->name == "my-tunnel" && tunnel(dnstt)->data["backend"].toObject()["type"].toString() == "ssh"
              && tunnel(dnstt)->data["backend"].toObject()["user"].toString() == "tunnel", "DNSTT dnst:// link with SSH backend");
        const auto vaydns = read("dnst://t.example.com/vaydns/socks?pubkey=" + key + "&record-type=txt&clientid-size=2&keepalive=10s&idle-timeout=60s#my-tunnel");
        check(vaydns && vaydns->type == "vaydns" && tunnel(vaydns)->data["clientid_size"].toInt() == 2 && tunnel(vaydns)->data["idle_timeout"].toString() == "60s",
              "VayDNS dnst:// link takes the VayDNS profile type");
        const auto compat = read("dnst://t.example.com/vaydns/socks?pubkey=" + key + "&dnstt-compat=true");
        check(compat && tunnel(compat)->data["dnstt_compat"].toBool(), "VayDNS DNSTT compatibility flag");
        const auto slip = read("dnst://a.example.com/slipstream/shadowsocks?cert=MIIBkTCB&method=aes-256-gcm&password=secret#my-tunnel");
        check(slip && slip->type == "slipstream" && tunnel(slip)->data["cert"].toString() == "MIIBkTCB", "SlipStream dnst:// link");
        const auto master = read("dnst://v.example.com/masterdns/socks?mkey=abcdef1234&encryption=5#my-tunnel");
        check(master && master->type == "masterdns" && tunnel(master)->data["encryption"].toInt() == 5 && tunnel(master)->data["mkey"].toString() == "abcdef1234",
              "MasterDNS dnst:// link");
        const auto json = read("dnst://eyJ0YWciOiJteS10dW5uZWwiLCJ0cmFuc3BvcnQiOnsidHlwZSI6InNsaXBzdHJlYW0iLCJkb21haW4iOiJhLmV4YW1wbGUuY29tIiwiY2VydCI6Ii0tLS0tQkVHSU4gQ0VSVElGSUNBVEUtLS0tLVxuTUlJQmtUQ0IuLi5cbi0tLS0tRU5EIENFUlRJRklDQVRFLS0tLS0ifSwiYmFja2VuZCI6eyJ0eXBlIjoic2hhZG93c29ja3MiLCJtZXRob2QiOiJhZXMtMjU2LWdjbSIsInBhc3N3b3JkIjoic2VjcmV0In19");
        check(json && json->type == "slipstream" && tunnel(json)->name == "my-tunnel" && tunnel(json)->data["cert"].toString().startsWith("-----BEGIN CERTIFICATE-----")
              && tunnel(json)->data["backend"].toObject()["method"].toString() == "aes-256-gcm", "dnst:// base64 JSON form");
        check(!read("dnst://t.example.com/dnstt/socks?pubkey=ab3f...c8") && !read("dnst://t.example.com/dnstt/ssh?pubkey=" + key)
              && !read("dnst://t.example.com/smoke-signals/socks?pubkey=" + key), "invalid DNS tunnel profiles are rejected");
        const auto slipnet = [](const QStringList &fields) { return "slipnet://" + QString::fromLatin1(fields.join('|').toUtf8().toBase64()); };
        const auto home = read(slipnet({"28", "dnstt", "Home", "t.example.com", "8.8.8.8:53:0,1.1.1.1:53:0", "0", "5000", "bbr", "1080", "127.0.0.1", "0", key, "proxyuser", "proxypass"}));
        check(home && home->type == "dnstt" && tunnel(home)->name == "Home" && tunnel(home)->data["resolvers"].toArray() == QJsonArray{"8.8.8.8:53", "1.1.1.1:53"}
              && tunnel(home)->data["backend"].toObject()["user"].toString() == "proxyuser", "SlipNet DNSTT export");
        QStringList fields(50);
        const QStringList head{"28", "ss", "Slip", "s.example.com", "9.9.9.9:53:1", "1", "400", "dcubic", "1080", "127.0.0.1", "0"};
        for (int i = 0; i < head.size(); ++i) fields[i] = head[i];
        const auto slipnetSlip = read(slipnet(fields));
        check(slipnetSlip && slipnetSlip->type == "slipstream" && tunnel(slipnetSlip)->data["authoritative"].toBool()
              && tunnel(slipnetSlip)->data["congestion_control"].toString() == "dcubic", "SlipNet SlipStream export");
        const QByteArray white = R"({"schema":"whitedns.profile","version":1,"profile":{"name":"Iran","server":{"domain":"v.example.com","encryption_key":"k3y","encryption_method":2}}})";
        const auto whiteDns = read("masterdns://" + QString::fromLatin1(white.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
        check(whiteDns && whiteDns->type == "masterdns" && tunnel(whiteDns)->name == "Iran" && tunnel(whiteDns)->data["encryption"].toInt() == 2, "WhiteDNS MasterDNS link");
        for (const auto &link : {QString("dnst://t.example.com/vaydns/ssh?pubkey=" + key + "&record-type=cname&user=tunnel&password=secret#Мой туннель"),
                                 QString("dnst://v.example.com/masterdns/socks?mkey=abcdef1234&encryption=3"), QString("dnst://a.example.com/slipstream/socks?cert=MIIBkTCB")}) {
            const auto first = read(link);
            const auto again = first ? read(tunnel(first)->ExportToLink()) : nullptr;
            check(again && again->type == first->type && tunnel(again)->ExportToJson() == tunnel(first)->ExportToJson(), "DNS tunnel exported link reads back");
        }
        if (dnstt && tunnel(dnstt)->Prepare().isEmpty()) {
            const auto prepared = QJsonDocument::fromJson(tunnel(dnstt)->extraCoreConf.toUtf8()).object();
            check(prepared["backend"].toString() == "dnstt" && prepared["tunnel_backend"].toObject()["type"].toString() == "ssh"
                  && !prepared["socks_user"].toString().isEmpty() && tunnel(dnstt)->Build().object["network"].toString() == "tcp",
                  "DNS tunnel helper gets its transport, service and per-launch credentials");
        }
        const QByteArray s3 = R"({"log":{"loglevel":"info"},"inbounds":[{"tag":"socks-in","listen":"127.0.0.1","port":10808,"protocol":"socks","settings":{"auth":"noauth","udp":true}}],
            "outbounds":[{"tag":"proxy","protocol":"fedarisha","settings":{"storage":{"type":"s3","bucket":"vlt-alpha","endpoint":"https://storage.example.ru","region":"ru-msk",
            "prefix":"fedarisha/4/","sessionsDir":"sessions","accessKey":"AK","secretKey":"SK"},"tuning":{"idleTimeoutSec":300,"pollIntervalMs":100,"writeIntervalMs":20,"maxFileSizeBytes":2097152}}},
            {"tag":"direct","protocol":"freedom","settings":{}}]})";
        const auto s3profiles = parse(s3);
        auto *bypass = s3profiles.size() == 1 ? dynamic_cast<Configs::s3bypass *>(s3profiles.first()->outbound.get()) : nullptr;
        check(bypass && s3profiles.first()->type == "s3bypass" && bypass->storage["bucket"].toString() == "vlt-alpha" && bypass->tuning["pollIntervalMs"].toInt() == 100
              && bypass->DisplayAddress() == "storage.example.ru/vlt-alpha", "S3 Bypass Xray configuration of the original app");
        Configs::s3bypass empty;
        check(!empty.ParseFromJson(QJsonObject{{"type", "s3bypass"}}), "S3 Bypass without storage is rejected");
        // Files the original apps keep: MasterDnsVPN's client_config.toml and WhiteDNS's profile JSON.
        const auto toml = parse("# MasterDnsVPN client\nDOMAINS = [\"v.example.com\"]\nDATA_ENCRYPTION_METHOD = 3\nENCRYPTION_KEY = \"k3y-value\"\nPROTOCOL_TYPE = \"SOCKS5\"\nLISTEN_PORT = 18000\n");
        check(toml.size() == 1 && toml.first()->type == "masterdns" && tunnel(toml.first())->data["mkey"].toString() == "k3y-value"
              && tunnel(toml.first())->data["encryption"].toInt() == 3 && tunnel(toml.first())->data["domain"].toString() == "v.example.com", "MasterDnsVPN client_config.toml file");
        const auto whiteFile = parse(white);
        check(whiteFile.size() == 1 && whiteFile.first()->type == "masterdns" && tunnel(whiteFile.first())->name == "Iran", "WhiteDNS profile file");
    }
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
        bool quicRefused = false;
        for (const auto &rule : live->coreConfig["route"].toObject()["rules"].toArray())
            quicRefused |= rule.toObject() == QJsonObject{{"network", "udp"}, {"port", 443}, {"action", "reject"}};
        check(quicRefused, "QUIC is refused so browsers fall back to TCP");
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
        check(profile->mode == "wg" && profile->hashes.size() == 2, "WG default and hashes");
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
        check(profile && profile->workers == 9 && profile->mode == "wg", "normalize worker count and keep the WG default");
        const auto roundTrip = parse(profiles.first()->outbound->ExportToLink().toUtf8());
        check(roundTrip.size() == 1 && roundTrip.first()->outbound->ExportIdentity() == profiles.first()->outbound->ExportIdentity(), "normalized import round trip");
    }
    const QByteArray subscription = R"({"subscriptionName":"Demo","profiles":[{"name":"First","peer":"203.0.113.10","pass":"password","hashes":"hash1"},{"name":"Second","peer":"203.0.113.11:56000","password":"password","vkHashes":"hash2","mode":"wg"}]})";
    for (const auto &body : {subscription, subscription.toBase64()}) {
        const auto profiles = parse(body);
        check(profiles.size() == 2, "Android subscription");
        if (profiles.size() != 2) continue;
        check(profiles.first()->outbound->DisplayType() == "qWDTT (WG)" && profiles.last()->outbound->DisplayType() == "qWDTT (WG)", "subscription modes and ordering");
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
        check(deployed.hashSource == "auto" && deployed.hashes.isEmpty(), "deployment profile inherits shared automatic hashes without persisting account calls");
        Configs::dataManager->profilesRepo->AddProfile(profile, fixtureGroups.first());
        auto plusProfile = Configs::ProfilesRepo::NewProfile("wdttplus");
        auto csProfile = Configs::ProfilesRepo::NewProfile("csqtt");
        auto ownTokenProfile = Configs::ProfilesRepo::NewProfile("csqtt");
        auto *plus = dynamic_cast<Configs::wdttplus *>(plusProfile->outbound.get());
        auto *cs = dynamic_cast<Configs::csqtt *>(csProfile->outbound.get());
        auto *ownToken = dynamic_cast<Configs::csqtt *>(ownTokenProfile->outbound.get());
        plus->hashSource = "auto";
        cs->hashSource = "auto_js"; cs->vkAnonPath = "auto_js";
        ownToken->hashSource = "auto_js"; ownToken->vkAccessToken = "profile-token";
        for (auto item : {plusProfile, csProfile, ownTokenProfile}) Configs::dataManager->profilesRepo->AddProfile(item, fixtureGroups.first());
        check(ResetQWDTTHashSources(), "unlink saves reset hash sources");
        check(dynamic_cast<Configs::qwdtt *>(profile->outbound.get())->hashSource == "manual", "unlink resets existing automatic profile");
        check(plus->hashSource == "manual" && cs->hashSource == "manual" && cs->vkAnonPath == "vkcalls", "unlink resets all shared relay modes even in incomplete editable profiles");
        check(ownToken->hashSource == "auto_js" && ownToken->vkAccessToken == "profile-token", "unlink preserves a profile's own JS token");
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
    {
        auto *settings = Configs::dataManager->settingsRepo.get();
        auto &db = Configs::dataManager->getDatabase();
        settings->log_level = "debug";
        settings->url_test_timeout_ms = 12345;
        settings->Save();
        const auto device = settings->relay_device_id;
        const auto secret = settings->core_box_api_secret;
        const auto counts = [&db] {
            QMap<QString, qlonglong> result;
            auto tables = db.queryThrow("SELECT name FROM sqlite_master WHERE type='table' AND name <> 'settings'");
            while (tables->executeStep()) {
                const auto name = QString::fromStdString(tables->getColumn(0).getText());
                auto quoted = name; quoted.replace('"', "\"\"");
                auto count = db.queryThrow(("SELECT count(*) FROM \"" + quoted + "\"").toStdString());
                if (count->executeStep()) result[name] = count->getColumn(0).getInt64();
            }
            return result;
        };
        const auto before = counts();
        QTimer::singleShot(0, [] {
            if (auto *confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                confirmation->done(QMessageBox::No);
        });
        check(!DialogBasicSettings::RestoreDefaultsWithConfirmation(nullptr), "cancel reset confirmation");
        check(!settings->noSave && settings->log_level == "debug" && counts() == before,
              "cancel reset leaves settings and data intact");
        check(settings->ResetToDefaults(), "reset application settings transaction");
        settings->Save(); // An exit-time save must not restore stale preferences.
        check(settings->noSave, "reset prevents stale shutdown save");
        check(before == counts(), "reset retains every non-settings table");
        Configs::SettingsRepo defaults(db);
        check(defaults.log_level == "info" && defaults.url_test_timeout_ms == 3000 &&
              defaults.seasonal_decoration == "auto", "reset loads compiled defaults after restart");
        check(defaults.relay_device_id == device && defaults.core_box_api_secret == secret,
              "reset preserves bound device and API identity");
    }
    qInfo() << "qWDTT import and release version tests:" << failures << "failures";
    return failures == 0 ? 0 : 1;
}
