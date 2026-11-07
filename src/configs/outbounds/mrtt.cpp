#include "include/configs/outbounds/mrtt.h"
#include "include/configs/outbounds/qwdtt.h"
#include "include/global/Configs.hpp"
#include "include/ui/setting/dialog_vk_relay.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTcpServer>
#include <QUdpSocket>
#include <QUrl>
#include <QUuid>

namespace Configs {
namespace {
const QString scheme = "mrtt://";
QString endpoint(const QString &host, int port) { return (host.contains(':') ? "[" + host + "]" : host) + ":" + QString::number(port); }
QString helperPath(const QString &name) {
    QString file = name;
#ifdef Q_OS_WIN
    file += ".exe";
#endif
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(file);
}
QString queryDecode(const QString &value) { return QUrl::fromPercentEncoding(value.toUtf8()); }
// parseRawQuery of the original: a repeated key is refused, concatenated parameters of older panels are still read.
bool queryValues(QString query, QMap<QString, QString> &values) {
    query.replace("&amp;", "&");
    for (const auto &part : query.split(QRegularExpression("[&;]"), Qt::SkipEmptyParts)) {
        const int separator = part.indexOf('=');
        if (separator <= 0) continue;
        const auto key = queryDecode(part.left(separator));
        if (values.contains(key)) return false;
        values[key] = part.mid(separator + 1);
    }
    if (values.contains("v") && values.contains("host") && values.contains("peer")) return true;
    values.clear();
    static const QRegularExpression keys("(v|host|peer|password|client|hashes|token|max|jitsi|seats|workers|transport)=", QRegularExpression::CaseInsensitiveOption);
    QList<QRegularExpressionMatch> found;
    for (auto matches = keys.globalMatch(query); matches.hasNext();) found.append(matches.next());
    for (int i = 0; i < found.size(); ++i) {
        const auto key = found[i].captured(1).toLower();
        if (values.contains(key)) return false;
        const int end = i + 1 < found.size() ? found[i + 1].capturedStart() : query.size();
        values[key] = query.mid(found[i].capturedEnd(), end - found[i].capturedEnd());
    }
    return values.contains("v") && values.contains("host") && values.contains("peer");
}
}

mrtt::mrtt() {
    server_port = 46000; socksPort = 0;
    // New profiles follow the default hash source of the VK binding, as the other VK transports do.
    if (dataManager) { const auto *settings = dataManager->settingsRepo.get(); hashSource = settings->vk_relay_linked ? settings->qwdtt_hash_source : "manual"; }
}
QStringList mrtt::NormalizeHashes(const QStringList &input) {
    QStringList result;
    for (const auto &item : input) {
        const auto hash = qwdtt::StripVkCallLink(item);
        if (!hash.isEmpty()) result.append(hash);
    }
    return result;
}
int mrtt::MaxWorkersFor(int hashCount, bool anyCount) {
    return anyCount ? MaxWorkers / WorkersPerGroup * WorkersPerGroup : qBound(1, hashCount, int(MaxHashes)) * GroupsPerHash * WorkersPerGroup;
}
int mrtt::NormalizeWorkers(int requested, int hashCount, bool anyCount) {
    return qBound(int(WorkersPerGroup), requested, MaxWorkersFor(hashCount, anyCount)) / WorkersPerGroup * WorkersPerGroup;
}
bool mrtt::ValidJitsiRoom(const QString &room) { return QRegularExpression("^[a-z0-9.:-]+/[a-z0-9]{1,64}$").match(room).hasMatch(); }
QString mrtt::ParseJitsiRoom(const QString &input) {
    auto value = input.trimmed();
    if (value.startsWith("https://")) value.remove(0, 8);
    else if (value.startsWith("http://")) value.remove(0, 7);
    value = value.section('?', 0, 0).section('#', 0, 0);
    while (value.endsWith('/')) value.chop(1);
    value = value.toLower();
    return ValidJitsiRoom(value) ? value : QString();
}
bool mrtt::ValidMaxGroup(const QString &group) { return QRegularExpression("^-?[0-9]{1,64}$").match(group).hasMatch(); }
bool mrtt::ValidPassword(const QString &value) {
    return !value.isEmpty() && value.toUtf8().size() <= 128 && !value.contains(QRegularExpression("[|\\s\\x00-\\x1f\\x7f]"));
}
QString mrtt::SharePayload(const QString &value, bool encrypt) {
    if (value.isEmpty() || value.size() > 1 << 20) return {};
    QProcess process;
    process.start(helperPath("qwdtt"), {encrypt ? "-mrtt-share-encode" : "-mrtt-share-decode"});
    if (!process.waitForStarted(5000)) return {};
    process.write(value.toUtf8()); process.closeWriteChannel();
    if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return {};
    return QString::fromUtf8(process.readAllStandardOutput());
}
QString mrtt::DisplayType() {
    return QStringLiteral("MRTT (%1)").arg(transportMode == "max" ? "MAX" : transportMode == "jitsi" ? "Jitsi" : "VK");
}
bool mrtt::ParseFromJson(const QJsonObject &object) {
    if (object["type"].toString() != "mrtt") return false;
    outbound::ParseFromJson(object);
    password = object["password"].toString();
    transportMode = object["transport"].toString("vk");
    hashSource = object["hash_source"].toString(hashSource);
    vkAccessToken = object["vk_access_token"].toString();
    autoHashCount = qBound(1, object["auto_hash_count"].toInt(autoHashCount), int(MaxHashes));
    hashes.clear();
    for (const auto &hash : object["hashes"].toArray()) hashes.append(hash.toString());
    hashes = NormalizeHashes(hashes.mid(0, MaxHashes));
    maxGroup = object["max_group"].toString();
    jitsiRoom = object["jitsi_room"].toString();
    jitsiSeats = qBound(0, object["jitsi_seats"].toInt(), int(MaxJitsiWorkers));
    jitsiWorkers = qBound(int(MinJitsiWorkers), object["jitsi_workers"].toInt(6), int(MaxJitsiWorkers));
    turnTransport = object["turn_transport"].toString("udp");
    srtpMode = object["srtp_mode"].toString("auto");
    fingerprint = object["fingerprint"].toString("firefox");
    clientIds = object["client_ids"].toString("8202606,6287487");
    customTurnIps = object["custom_turn_ips"].toString();
    vkAnonPath = object["vk_anon_path"].toString("vkcalls");
    captchaMode = object["captcha_mode"].toString("auto");
    mtu = object["mtu"].toInt(1280);
    workers = NormalizeWorkers(object["workers"].toInt(18), hashSource == "auto" ? autoHashCount : hashes.size(), hashSource != "manual" || transportMode == "max");
    return !server.isEmpty() && server_port > 0 && server_port <= 65535
        && (transportMode == "vk" || transportMode == "max" || transportMode == "jitsi")
        && (hashSource == "manual" || hashSource == "auto" || hashSource == "auto_js")
        && (turnTransport == "udp" || turnTransport == "tcp_tls")
        && (srtpMode == "auto" || srtpMode == "wrap" || srtpMode == "dtls")
        && (vkAnonPath == "vkcalls" || vkAnonPath == "legacy")
        && (captchaMode == "auto" || captchaMode == "wv" || captchaMode == "rjs")
        && (mtu == 1280 || mtu == 1400 || mtu == 1500)
        && (maxGroup.isEmpty() || ValidMaxGroup(maxGroup))
        && (jitsiRoom.isEmpty() || ValidJitsiRoom(jitsiRoom))
        && vkAccessToken.size() <= 4096 && !vkAccessToken.contains(QRegularExpression("\\s"));
}
// parseMrttLink / parseMrttV2 of the original app: only the encrypted form, no nested links.
bool mrtt::ParseFromLink(const QString &link) {
    const auto trimmed = link.trimmed();
    if (!trimmed.startsWith(scheme, Qt::CaseInsensitive) || trimmed.size() > 1 << 16) return false;
    const auto plain = SharePayload(trimmed, false);
    if (plain.isEmpty() || plain.contains("://") || (!plain.contains("host=") && !plain.contains("connect?"))) return false;
    QMap<QString, QString> values;
    if (!queryValues(plain.startsWith("connect?", Qt::CaseInsensitive) ? plain.mid(8) : plain, values)) return false;
    const auto value = [&values](const QString &key) { return queryDecode(values.value(key)); };
    if (value("v") != "2") return false;
    const auto host = value("host");
    bool ok = false;
    const int port = value("peer").toInt(&ok);
    if (host.isEmpty() || host == "0.0.0.0" || host == "127.0.0.1" || host == "::1" || host.compare("localhost", Qt::CaseInsensitive) == 0 || host.contains(QRegularExpression("\\s"))) return false;
    if (!ok || port < 1 || port > 65535) return false;
    const auto secret = value("password").trimmed();
    if (secret.contains(QRegularExpression("\\s"))) return false;
    QJsonObject object{{"type", "mrtt"}, {"tag", host}, {"server", host}, {"server_port", port}, {"password", secret}, {"hash_source", "manual"}};
    if (values.contains("hashes")) {
        const auto parts = values["hashes"].split('+');
        if (parts.size() > MaxHashes || parts.contains(QString())) return false;
        QStringList list;
        for (const auto &part : parts) list.append(qwdtt::StripVkCallLink(queryDecode(part)));
        for (const auto &hash : list) if (hash.size() < 16 || hash.contains(QRegularExpression("\\s")) || list.count(hash) > 1) return false;
        object["hashes"] = QJsonArray::fromStringList(list);
    }
    if (values.contains("token")) {
        const auto token = value("token").trimmed();
        if (token.isEmpty() || token.size() > 4096 || token.contains(QRegularExpression("\\s")) || values.contains("hashes")) return false;
        object["vk_access_token"] = token;
        object["hash_source"] = "auto_js";
    }
    if (values.contains("max")) {
        const auto group = value("max").trimmed();
        if (!ValidMaxGroup(group) || values.contains("hashes") || values.contains("token")) return false;
        object["max_group"] = group;
    }
    if (values.contains("jitsi")) {
        const auto room = value("jitsi").trimmed().toLower();
        if (!ValidJitsiRoom(room)) return false;
        object["jitsi_room"] = room;
    }
    object["jitsi_seats"] = qBound(0, value("seats").toInt(), int(MaxJitsiWorkers));
    const auto carried = value("transport").trimmed().toLower();
    // The transport comes from the link, VK when it names none; MAX is chosen by hand in this client.
    object["transport"] = carried == "jitsi" ? "jitsi" : "vk";
    return ParseFromJson(object);
}
QJsonObject mrtt::ExportToJson() {
    QJsonObject object{{"type", "mrtt"}, {"tag", name}, {"server", server}, {"server_port", server_port}, {"password", password}, {"transport", transportMode},
        {"hash_source", hashSource}, {"vk_access_token", vkAccessToken}, {"auto_hash_count", autoHashCount}, {"hashes", QJsonArray::fromStringList(hashes)},
        {"workers", workers}, {"max_group", maxGroup}, {"jitsi_room", jitsiRoom}, {"jitsi_seats", jitsiSeats}, {"jitsi_workers", jitsiWorkers},
        {"turn_transport", turnTransport}, {"srtp_mode", srtpMode}, {"fingerprint", fingerprint}, {"client_ids", clientIds}, {"custom_turn_ips", customTurnIps},
        {"vk_anon_path", vkAnonPath}, {"captcha_mode", captchaMode}, {"mtu", mtu}, {"manual_hashes_override", manualRelayHashes}};
    return object;
}
QJsonObject mrtt::ExportIdentity() { auto object = ExportToJson(); object.remove("tag"); object.remove("hashes"); object.remove("workers"); object.remove("manual_hashes_override"); return object; }
// formatMrttLink of the original.
QString mrtt::ExportToLink() {
    const auto encode = [](const QString &value) { return QString::fromLatin1(QUrl::toPercentEncoding(value)); };
    QStringList query{"v=2", "host=" + server, "peer=" + QString::number(server_port), "password=" + encode(password)};
    if (!maxGroup.isEmpty()) query << "max=" + maxGroup;
    else if (hashSource == "manual" && !hashes.isEmpty()) query << "hashes=" + hashes.join('+');
    else if (hashSource == "auto_js" && !vkAccessToken.isEmpty()) query << "token=" + encode(vkAccessToken);
    if (!jitsiRoom.isEmpty()) {
        query << "jitsi=" + encode(jitsiRoom);
        if (jitsiSeats > 0) query << "seats=" + QString::number(jitsiSeats);
    }
    if (transportMode == "vk" || transportMode == "jitsi") query << "transport=" + transportMode;
    return SharePayload("connect?" + query.join('&'), true);
}
QString mrtt::Prepare() {
    if (server.isEmpty() || server_port < 1 || server_port > 65535) return QObject::tr("MRTT: invalid server address or port");
    if (!ValidPassword(password)) return QStringLiteral("MRTT: пароль без пробелов и «|», не длиннее 128 байт");
    if (!QFileInfo(helperPath("qwdtt")).isExecutable() || !QFileInfo(helperPath("mrtt-transport")).isExecutable()) return QStringLiteral("MRTT: клиент не входит в эту сборку");
    QTcpServer tcp; QUdpSocket udp;
    if (!tcp.listen(QHostAddress::LocalHost, 0) || !udp.bind(QHostAddress::LocalHost, 0)) return QStringLiteral("MRTT: не удалось выбрать локальные порты");
    socksAddress = "127.0.0.1"; socksPort = tcp.serverPort(); bridgeAuth = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject config{{"backend", "mrtt"}, {"peer", endpoint(server, server_port)}, {"password", password}, {"transport", transportMode},
        {"device_id", RelayDeviceID()}, {"device_name", QSysInfo::machineHostName().left(64).remove('|')}, {"obfs", "unified"}, {"turn_transport", turnTransport}, {"srtp_mode", srtpMode},
        {"fingerprint", fingerprint}, {"client_ids", clientIds}, {"custom_turn_ips", customTurnIps}, {"captcha_mode", captchaMode}, {"vk_auth_mode", vkAnonPath}, {"mtu", mtu},
        {"listen", endpoint("127.0.0.1", udp.localPort())}, {"socks", endpoint(socksAddress, socksPort)}, {"socks_user", bridgeAuth}, {"socks_pass", bridgeAuth}};
    if (transportMode == "jitsi") {
        if (!ValidJitsiRoom(jitsiRoom)) return QStringLiteral("MRTT: вставьте ссылку на комнату Jitsi");
        config["jitsi_room"] = jitsiRoom;
        // A worker is a room participant: the seats the server named in the link, otherwise the own count.
        config["workers"] = qBound(int(MinJitsiWorkers), jitsiSeats > 0 ? jitsiSeats : jitsiWorkers, int(MaxJitsiWorkers));
    } else if (transportMode == "max") {
        QString error;
        const auto account = RelayMaxAccount(&error);
        if (!error.isEmpty()) return error;
        config["max_token"] = account["token"].toString();
        config["max_group"] = maxGroup.isEmpty() ? account["group"].toString() : maxGroup;
        config["workers"] = NormalizeWorkers(workers, 0, true);
    } else {
        QStringList effective = NormalizeHashes(hashes);
        if (hashSource == "auto_js") {
            auto token = vkAccessToken;
            if (token.isEmpty()) {
                QString error; token = RelayAccountToken(&error);
                if (!error.isEmpty()) return error;
            }
            config["vk_access_token"] = token;
            config["vk_hash_mode"] = "auto_js";
            config["vk_auth_mode"] = "auto_js";
            effective.clear();
        } else if (hashSource == "auto") {
            QString error; effective = NormalizeHashes(RelayAutoHashes("mrtt", server, server_port, autoHashCount, &error));
            if (!error.isEmpty()) return error;
        }
        if (hashSource != "auto_js" && (effective.isEmpty() || effective.size() > MaxHashes)) return QStringLiteral("MRTT: укажите от одного до шести хешей звонков VK");
        for (const auto &hash : effective)
            if (hash.size() < 16 || hash.contains(QRegularExpression("[,|\\s]")) || effective.count(hash) > 1) return QStringLiteral("MRTT: некорректный или повторяющийся хеш VK");
        config["hashes"] = QJsonArray::fromStringList(effective);
        config["workers"] = NormalizeWorkers(workers, effective.size(), hashSource == "auto_js");
    }
    extraCorePath = helperPath("qwdtt");
    extraCoreConf = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact)); extraCoreArgs = "-mrtt-config %s";
    return {};
}
BuildResult mrtt::Build() {
    if (socksPort == 0 || bridgeAuth.isEmpty()) return {{}, "MRTT bridge has not been prepared"};
    return {QJsonObject{{"type", "socks"}, {"server", socksAddress}, {"server_port", socksPort}, {"version", "5"}, {"username", bridgeAuth}, {"password", bridgeAuth}}, {}};
}
}
