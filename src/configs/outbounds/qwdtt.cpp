#include "include/configs/outbounds/qwdtt.h"
#include "include/configs/common/utils.h"
#include "include/global/Configs.hpp"
#include "include/ui/setting/dialog_vk_relay.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QMessageAuthenticationCode>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTimer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QUdpSocket>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>

namespace {
bool validPort(int p) { return p >= 1 && p <= 65535; }
bool setPeer(Configs::qwdtt &o, const QString &peer) {
    const QUrl url(QStringLiteral("udp://") + peer);
    if (!url.isValid() || url.host().isEmpty()) return false;
    o.server = url.host(QUrl::FullyEncoded);
    o.server_port = url.port(56000);
    return validPort(o.server_port);
}
QString peerString(const QString &host, int port) {
    return (host.contains(':') ? "[" + host + "]" : host) + ":" + QString::number(port);
}
QString normalizedMode(const QString &m) {
    // The original app connects in its VPN (WireGuard) mode unless RAW is chosen.
    if (m.isEmpty()) return "wg";
    if (m == "raw" || m == "rawtun") return "raw";
    return m == "wg" || m == "vpn" || m == "socks" ? "wg" : m;
}
}

namespace Configs {
QString qwdtt::NativePingConfig(const QString &transportConfig) const {
    auto config = QJsonDocument::fromJson(transportConfig.toUtf8()).object();
    config["peer"] = peerString(server, server_port);
    config["mode"] = "wg";
    config["probe_only"] = true;
    config["interactive_captcha"] = false;
    return QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
}
qwdtt::qwdtt() {
    server_port = 56000; socksPort = 0; deviceID = RelayDeviceID();
    if (dataManager) { const auto *settings = dataManager->settingsRepo.get(); hashSource = settings->vk_relay_linked ? settings->qwdtt_hash_source : "manual"; autoHashCount = qBound(1, settings->qwdtt_auto_hash_count, 4); }
}
QString qwdtt::StripVkCallLink(const QString &input) {
    auto s = input.trimmed();
    for (const auto *prefix : {"https://vk.com/call/join/", "http://vk.com/call/join/", "https://m.vk.com/call/join/", "http://m.vk.com/call/join/",
                               "https://vk.ru/call/join/", "http://vk.ru/call/join/", "https://m.vk.ru/call/join/", "http://m.vk.ru/call/join/",
                               "m.vk.com/call/join/", "vk.com/call/join/", "m.vk.ru/call/join/", "vk.ru/call/join/"}) {
        if (s.startsWith(QLatin1String(prefix), Qt::CaseInsensitive)) { s = s.mid(int(qstrlen(prefix))); break; }
    }
    s = s.section('?', 0, 0).section('#', 0, 0);
    while (s.endsWith('/')) s.chop(1);
    // Any other web link (a redirect, a short form) still names the call last.
    if (s.startsWith("https://", Qt::CaseInsensitive) || s.startsWith("http://", Qt::CaseInsensitive)) s = QUrl(s).path().section('/', -1);
    return s;
}

QStringList qwdtt::NormalizeHashes(const QStringList &input) {
    QStringList out;
    for (const auto &item : input) {
        for (auto h : item.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts)) {
            h = StripVkCallLink(h);
            if (!h.isEmpty() && !out.contains(h)) out.append(h);
        }
    }
    return out;
}

bool qwdtt::ParseFromJson(const QJsonObject &o) {
    hashSource = o["hash_source"].toString(hashSource);
    if (hashSource != "auto" && hashSource != "manual") return false;
    autoHashCount = qBound(1, o["auto_hash_count"].toInt(autoHashCount), 4);
    const auto type = o["type"].toString();
    if (!type.isEmpty() && type != "qwdtt" && type != "wdtt") return false;
    outbound::ParseFromJson(o);
    if (o.contains("peer") && !setPeer(*this, o["peer"].toString())) return false;
    if (o.contains("name")) name = o["name"].toString();
    password = o.contains("password") ? o["password"].toString() : o["pass"].toString();
    mode = normalizedMode(o["mode"].toString().toLower());
    rawPort = o["raw_port"].toInt(56003);
    workers = o["workers"].toInt(o["workersPerHash"].toInt(9));
    turnTCP = o["turn_tcp"].toBool(true);
    obfs = o["obfs"].toString("audio");
    vkAnonPath = o["vk_anon_path"].toString("vkcalls");
    captchaMode = o["captcha_mode"].toString("auto");
    goDNS = o["go_dns"].toString("yandex");
    if (!o["device_id"].toString().isEmpty()) deviceID = o["device_id"].toString();
    legacy = o["legacy"].toBool(false);
    wgPort = o["wg_port"].toInt(56001);
    localPort = o["local_port"].toInt(9000);
    const auto h = o.contains("hashes") ? o["hashes"] : o["vkHashes"];
    QStringList values;
    if (h.isArray()) { for (const auto &v : h.toArray()) values.append(v.toString()); }
    else values.append(h.toString());
    hashes = NormalizeHashes(values).mid(0, 4);
    workers = NormalizeWorkers(workers, hashSource == "auto" ? autoHashCount : hashes.size());
    // Empty hashes are editable after importing Android profiles using global hashes.
    return !server.isEmpty() && validPort(server_port) && validPort(rawPort) && validPort(wgPort) && validPort(localPort) && hashes.size() <= 4 && (mode == "raw" || mode == "wg") && (vkAnonPath == "vkcalls" || vkAnonPath == "legacy") && (captchaMode == "auto" || captchaMode == "wv" || captchaMode == "rjs");
}

bool qwdtt::ParseFromLink(const QString &link) {
    QString value = link.trimmed();
    if (value.startsWith("qwdtt:config", Qt::CaseInsensitive)) value.replace(0, 12, "qwdtt://config");
    if (value.startsWith("qwdtt://", Qt::CaseInsensitive)) {
        const QUrl u(value);
        if (!u.isValid() || u.host().compare("config", Qt::CaseInsensitive) != 0) return false;
        const QUrlQuery q(u);
        QJsonObject o{{"type", "qwdtt"}};
        for (const auto &key : {"peer", "name", "pass", "password", "hashes", "mode", "obfs", "go_dns", "device_id", "vk_anon_path", "captcha_mode", "hash_source"}) {
            if (q.hasQueryItem(key)) o[key] = formDecodedQueryValue(q, key);
        }
        for (const auto &key : {"raw_port", "workers", "auto_hash_count"}) {
            if (q.hasQueryItem(key)) { bool ok; int n = q.queryItemValue(key).toInt(&ok); if (!ok) return false; o[key] = n; }
        }
        if (q.hasQueryItem("turn_tcp")) o["turn_tcp"] = q.queryItemValue("turn_tcp") == "1" || q.queryItemValue("turn_tcp") == "true";
        if (!o.contains("name") && u.hasFragment()) o["name"] = u.fragment(QUrl::FullyDecoded);
        if (!q.hasQueryItem("peer")) return false;
        if (q.hasQueryItem("dtls_port") || q.hasQueryItem("server_port")) {
            if (!setPeer(*this, o["peer"].toString())) return false;
            bool ok;
            int p = q.queryItemValue(q.hasQueryItem("dtls_port") ? "dtls_port" : "server_port").toInt(&ok);
            if (!ok || !validPort(p)) return false;
            o["peer"] = peerString(server, p);
        }
        return ParseFromJson(o);
    }
    // wdtt://<server>:<dtls>:<wg>:<local>:<password>[:<hashes>][#name], as a qWDTT server shares it; wdtt://connect is WDTT Plus.
    if (!value.startsWith("wdtt://", Qt::CaseInsensitive) || value.startsWith("wdtt://connect?", Qt::CaseInsensitive)) return false;
    value.remove(0, 7);
    const int fragment = value.indexOf('#');
    QString label;
    if (fragment >= 0) { label = QUrl::fromPercentEncoding(value.mid(fragment + 1).toUtf8()); value.truncate(fragment); }
    const auto parts = value.split(':');
    if (parts.size() < 5 || parts.size() > 6) return false;
    bool ok;
    const int p = parts[1].toInt(&ok);
    if (!ok || !validPort(p)) return false;
    const int wg = parts[2].toInt(&ok);
    if (!ok || !validPort(wg)) return false;
    const int local = parts[3].toInt(&ok);
    if (!ok || !validPort(local)) return false;
    QJsonObject o{{"type", "qwdtt"}, {"legacy", true}, {"peer", peerString(parts[0], p)}, {"wg_port", wg}, {"local_port", local},
                  {"name", label}, {"password", QUrl::fromPercentEncoding(parts[4].toUtf8())}};
    if (parts.size() == 6) o["hashes"] = QUrl::fromPercentEncoding(parts[5].toUtf8());
    return ParseFromJson(o);
}

QJsonObject qwdtt::ExportToJson() {
    QJsonObject o{{"type", "qwdtt"}, {"tag", name}, {"server", server}, {"server_port", server_port},
            {"password", password}, {"hashes", QJsonArray::fromStringList(hashes)}, {"mode", mode},
            {"raw_port", rawPort}, {"workers", workers}, {"turn_tcp", turnTCP},
            {"obfs", obfs}, {"go_dns", goDNS}, {"device_id", deviceID},
            {"vk_anon_path", vkAnonPath}, {"captcha_mode", captchaMode}, {"manual_hashes_override", manualRelayHashes}, {"hash_source", hashSource}, {"auto_hash_count", autoHashCount}};
    if (legacy) { o["legacy"] = true; o["wg_port"] = wgPort; o["local_port"] = localPort; }
    return o;
}
QJsonObject qwdtt::ExportIdentity() { auto o = ExportToJson(); o.remove("tag"); o.remove("device_id"); o.remove("hashes"); o.remove("workers"); o.remove("manual_hashes_override"); return o; }
QString qwdtt::ExportToLink() {
    if (legacy) return "wdtt://" + server + ":" + QString::number(server_port) + ":" + QString::number(wgPort) + ":" + QString::number(localPort) + ":"
        + QString::fromLatin1(QUrl::toPercentEncoding(password)) + ":" + hashes.join(',') + (name.isEmpty() ? "" : "#" + QString::fromLatin1(QUrl::toPercentEncoding(name)));
    QUrl u("qwdtt://config");
    QUrlQuery q;
    // Android uses form encoding: encode literal '+' so it survives import there.
    const auto addText = [&q](const QString &key, const QString &value) {
        q.addQueryItem(key, QString::fromLatin1(QUrl::toPercentEncoding(value)));
    };
    addText("peer", peerString(server, server_port));
    addText("pass", password);
    addText("hashes", hashes.join(','));
    q.addQueryItem("mode", mode);
    q.addQueryItem("raw_port", QString::number(rawPort));
    q.addQueryItem("workers", QString::number(workers));
    q.addQueryItem("turn_tcp", turnTCP ? "1" : "0");
    q.addQueryItem("obfs", obfs);
    q.addQueryItem("vk_anon_path", vkAnonPath);
    q.addQueryItem("captcha_mode", captchaMode);
    q.addQueryItem("hash_source", hashSource);
    q.addQueryItem("auto_hash_count", QString::number(autoHashCount));
    addText("go_dns", goDNS);
    addText("device_id", deviceID);
    if (!name.isEmpty()) addText("name", name);
    u.setQuery(q);
    return u.toString(QUrl::FullyEncoded);
}

QString qwdtt::Prepare() {
    QStringList effectiveHashes = hashes;
    if (hashSource == "auto") { QString error; effectiveHashes = RelayAccountHashes(autoHashCount, &error); if (!error.isEmpty()) return error; }
    effectiveHashes = NormalizeHashes(effectiveHashes).mid(0, 4);
    hashes = NormalizeHashes(hashes).mid(0, 4);
    workers = NormalizeWorkers(workers, effectiveHashes.size());
    if (server.isEmpty() || !validPort(server_port) || !validPort(rawPort)) return QObject::tr("qWDTT: invalid server address or port");
    if (password.isEmpty() || password.contains('|') || password.contains('\n') || password.contains('\r')) return QObject::tr("qWDTT: a valid connection password is required");
    if (effectiveHashes.isEmpty() || effectiveHashes.size() > 4) return QObject::tr("qWDTT: enter one to four VK call hashes");
    if (workers < 1 || workers > 108) return QObject::tr("qWDTT: workers must be between 1 and 108");
    if (mode != "raw" && mode != "wg") return QObject::tr("qWDTT: invalid tunnel mode");
    if (obfs != "audio" && obfs != "video") return QObject::tr("qWDTT: invalid obfuscation mode");
    if (vkAnonPath != "vkcalls" && vkAnonPath != "legacy") return QObject::tr("qWDTT: invalid VK authorization mode");
    if (captchaMode != "auto" && captchaMode != "wv" && captchaMode != "rjs") return QObject::tr("qWDTT: invalid captcha mode");
    if (deviceID.isEmpty()) deviceID = RelayDeviceID();
    if (deviceID.contains('|') || deviceID.contains('\n') || deviceID.contains('\r')) return QObject::tr("qWDTT: invalid device ID");
    QString executable = "qwdtt";
#ifdef Q_OS_WIN
    executable += ".exe";
#endif
    extraCorePath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(executable);
    if (!QFileInfo(extraCorePath).isExecutable()) return QObject::tr("qWDTT: bundled client is missing: %1").arg(extraCorePath);
    QTcpServer tcp;
    QUdpSocket udp;
    if (!tcp.listen(QHostAddress::LocalHost, 0) || !udp.bind(QHostAddress::LocalHost, 0)) return QObject::tr("qWDTT: cannot allocate loopback ports");
    socksAddress = "127.0.0.1";
    socksPort = tcp.serverPort();
    const auto auth = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject conf{{"peer", peerString(server, mode == "raw" ? rawPort : server_port)},
                     {"password", password}, {"hashes", QJsonArray::fromStringList(effectiveHashes)},
                     {"mode", mode}, {"workers", workers}, {"device_id", deviceID},
                     {"turn_tcp", turnTCP}, {"obfs", obfs}, {"go_dns", goDNS},
                     {"vk_anon_path", vkAnonPath}, {"captcha_mode", captchaMode},
                     {"listen", peerString("127.0.0.1", udp.localPort())},
                     {"socks", peerString("127.0.0.1", socksPort)}, {"socks_user", auth}, {"socks_pass", auth}};
    extraCoreConf = QString::fromUtf8(QJsonDocument(conf).toJson(QJsonDocument::Compact));
    extraCoreArgs = "-config %s";
    bridgeAuth = auth;
    return {};
}
BuildResult qwdtt::Build() {
    if (socksPort == 0 || bridgeAuth.isEmpty()) return {{}, "qWDTT bridge has not been prepared"};
    return {QJsonObject{{"type", "socks"}, {"server", socksAddress}, {"server_port", socksPort},
                        {"version", "5"}, {"username", bridgeAuth}, {"password", bridgeAuth}}, {}};
}

QUrlQuery qwdtt::ProfileProof(const QString &action, const QString &password, const QString &device, const QString &nonce) {
    const auto keyId = QCryptographicHash::hash(QByteArray("WDTT-PROFILE-ID-v1") + '\0' + password.toUtf8(), QCryptographicHash::Sha256).toHex();
    const auto proof = QMessageAuthenticationCode::hash((action + "\n" + device + "\n" + nonce).toUtf8(), password.toUtf8(), QCryptographicHash::Sha256).toHex();
    QUrlQuery form;
    form.addQueryItem("device_id", device);
    form.addQueryItem("nonce", nonce);
    form.addQueryItem("key_id", QString::fromLatin1(keyId));
    form.addQueryItem("proof", QString::fromLatin1(proof));
    return form;
}

namespace {
QJsonObject profileApiPost(const QString &url, const QByteArray &body) {
    QNetworkAccessManager manager;
    manager.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setTransferTimeout(5000);
    auto *reply = manager.post(request, body);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) return {};
    return QJsonDocument::fromJson(reply->readAll()).object();
}

QJsonObject profileApiSigned(const QString &server, int port, const QString &password, const QString &device, const QString &action) {
    if (server.isEmpty() || port < 1 || port > 65535 || password.isEmpty() || device.isEmpty()) return {};
    const auto host = server.contains(':') && !server.startsWith('[') ? "[" + server + "]" : server;
    const auto base = QStringLiteral("http://%1:%2/api/profile/").arg(host).arg(port);
    const auto nonce = profileApiPost(base + "challenge", {})["nonce"].toString();
    if (nonce.isEmpty()) return {};
    return profileApiPost(base + action, Configs::qwdtt::ProfileProof(action, password, device, nonce).toString(QUrl::FullyEncoded).toUtf8());
}
}

qwdtt::DeviceSlots qwdtt::FetchDeviceSlots(const QString &server, int port, const QString &password, const QString &device) {
    const auto o = profileApiSigned(server, port, password, device, "status");
    if (o.isEmpty()) return {};
    return {true, o["max_devices"].toInt(1), o["bound_devices"].toInt(), o["active_devices"].toInt(), o["is_current_bound"].toBool()};
}

bool qwdtt::UnbindDevice(const QString &server, int port, const QString &password, const QString &device) {
    return profileApiSigned(server, port, password, device, "unbind")["success"].toBool();
}
}
