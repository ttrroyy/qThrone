#include "include/configs/outbounds/csqtt.h"
#include "include/configs/outbounds/qwdtt.h"
#include "include/global/Configs.hpp"
#include "include/ui/setting/dialog_vk_relay.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTcpServer>
#include <QUdpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QProcess>

namespace {
QString csqttSharePayload(const QString &value, bool encrypt) {
    QString helper = "qwdtt";
#ifdef Q_OS_WIN
    helper += ".exe";
#endif
    if (value.size() > 65536) return {};
    QProcess process;
    process.start(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(helper),
        {encrypt ? "-csqtt-share-encode" : "-csqtt-share-decode"});
    if (!process.waitForStarted(5000)) return {};
    process.write(value.toUtf8()); process.closeWriteChannel();
    if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return {};
    return QString::fromUtf8(process.readAllStandardOutput());
}
}

namespace Configs {
csqtt::csqtt() {
    server_port = 46000; socksPort = 0; deviceID = RelayDeviceID();
    // New profiles follow the default hash source of the VK binding, as qWDTT profiles do.
    if (dataManager) { const auto *settings = dataManager->settingsRepo.get(); hashSource = settings->vk_relay_linked ? settings->qwdtt_hash_source : "manual"; autoHashCount = qBound(1, settings->qwdtt_auto_hash_count, 4); }
}
QStringList csqtt::NormalizeHashes(const QStringList &input) {
    QStringList result;
    for (const auto &item : input) {
        // The original app reads a pasted call link the same way as qWDTT.
        const auto hash = qwdtt::StripVkCallLink(item);
        if (!hash.isEmpty()) result.append(hash);
    }
    return result;
}
bool csqtt::ParseFromJson(const QJsonObject &object) {
    if (object["type"].toString() != "csqtt") return false;
    outbound::ParseFromJson(object);
    password = object["password"].toString();
    hashSource = object["hash_source"].toString(hashSource);
    if (hashSource != "auto" && hashSource != "manual" && hashSource != "auto_js") return false;
    autoHashCount = qBound(1, object["auto_hash_count"].toInt(autoHashCount), 6);
    vkAccessToken = object["vk_access_token"].toString();
    hashes.clear();
    for (const auto &hash : object["hashes"].toArray()) hashes.append(hash.toString());
    hashes = NormalizeHashes(hashes.mid(0,6));
    workers = NormalizeWorkers(object["workers"].toInt(18),hashSource == "auto" ? autoHashCount : hashes.size());
    turnTCP = object["turn_tcp"].toBool(false);
    obfs = object["obfs"].toString("unified");
    turnTransport = object["turn_transport"].toString(turnTCP ? "tcp_tls" : "udp");
    if (turnTransport == "tcp/tls" || turnTransport == "tcp-tls") turnTransport = "tcp_tls";
    srtpMode = object["srtp_mode"].toString("auto");
    if (srtpMode == "wrap-srtp" || srtpMode == "wrap_srtp" || srtpMode == "legacy") srtpMode = "wrap";
    if (srtpMode == "dtls-srtp" || srtpMode == "dtls_srtp") srtpMode = "dtls";
    fingerprint = object["fingerprint"].toString("firefox");
    clientIds = object["client_ids"].toString("8202606,6287487");
    customTurnIps = object["custom_turn_ips"].toString();
    vkAnonPath = object["vk_anon_path"].toString("vkcalls");
    if (hashSource == "auto_js" || (!object.contains("hash_source") && vkAnonPath == "auto_js")) {
        hashSource = "auto_js"; vkAnonPath = "auto_js";
    } else if (vkAnonPath == "auto_js") vkAnonPath = "vkcalls";
    captchaMode = object["captcha_mode"].toString("auto");
    version = object["csqtt_version"].toString("2.5.0");
    if (!object["device_id"].toString().isEmpty()) deviceID = object["device_id"].toString();
    return !server.isEmpty() && server_port > 0 && server_port <= 65535 && hashes.size() <= 6
        && (obfs == "audio" || obfs == "video" || obfs == "unified")
        && (turnTransport == "udp" || turnTransport == "tcp" || turnTransport == "tcp_tls")
        && (srtpMode == "auto" || srtpMode == "wrap" || srtpMode == "dtls")
        && vkAccessToken.size() <= 4096 && !vkAccessToken.contains(QRegularExpression("[\\s\\x00-\\x1f\\x7f]"))
        && (vkAnonPath == "vkcalls" || vkAnonPath == "legacy" || vkAnonPath == "auto_js")
        && (captchaMode == "auto" || captchaMode == "wv" || captchaMode == "rjs")
        && (version == "2.5.0" || version == "2.1.9");
}
bool csqtt::ParseFromLink(const QString &link) {
    if (link.size() > 65536) return false;
    const auto trimmed = link.trimmed();
    if (trimmed.startsWith("csqtt://", Qt::CaseInsensitive) && !trimmed.contains('?') && !trimmed.contains('@')) {
        auto plain = csqttSharePayload(trimmed, false);
        if (plain.isEmpty()) return false;
        if (plain.startsWith("csqtt://", Qt::CaseInsensitive)) plain.remove(0, 8);
        if (!plain.startsWith("connect?", Qt::CaseInsensitive)) plain.prepend("connect?");
        return ParseFromLink("csqtt://" + plain);
    }
    QUrl url(link.trimmed());
    if (!url.isValid() || url.scheme().compare("csqtt",Qt::CaseInsensitive)) return false;
    QJsonObject object{{"type","csqtt"}};
    if (url.hasFragment()) object["tag"] = url.fragment(QUrl::FullyDecoded);
    if (url.host().compare("connect",Qt::CaseInsensitive) == 0) {
        if (!url.userInfo().isEmpty() || url.port() != -1 || !url.path().isEmpty()) return false;
        auto query = url.query(QUrl::FullyEncoded).replace("&amp;","&");
        QMap<QString,QString> values;
        for (auto part : query.split(QRegularExpression("[&;]"))) {
            int index = part.indexOf('=');
            if (index > 0) { const auto key = part.left(index); if (values.contains(key)) return false; values[key] = part.mid(index+1); }
        }
        if (!values.contains("v") || !values.contains("host") || !values.contains("peer") || !values.contains("password")) {
            const QRegularExpression keys("(v|host|peer|password|hashes)=",QRegularExpression::CaseInsensitiveOption);
            auto matches = keys.globalMatch(query);
            QList<QRegularExpressionMatch> list;
            while (matches.hasNext()) list.append(matches.next());
            for (int i = 0; i < list.size(); ++i) {
                int end = i+1 < list.size() ? list[i+1].capturedStart() : query.size();
                values[list[i].captured(1).toLower()] = query.mid(list[i].capturedEnd(),end-list[i].capturedEnd());
            }
        }
        const auto decode = [&values](const QString &key) { return QUrl::fromPercentEncoding(values[key].toUtf8()); };
        for (const auto &key : {QStringLiteral("name"), QStringLiteral("remark")}) {
            if (!decode(key).isEmpty()) { object["tag"] = decode(key); break; }
        }
        if (decode("v") != "2") return false;
        object["server"] = decode("host");
        bool ok; int port = decode("peer").toInt(&ok); if (!ok) return false;
        object["server_port"] = port;
        object["password"] = decode("password");
        if (values.contains("device_id")) object["device_id"] = decode("device_id");
        if (values.contains("token")) {
            const auto token = decode("token");
            if (token.isEmpty() || values.contains("hashes")) return false;
            object["vk_access_token"] = token;
            object["hash_source"] = "auto_js";
            object["vk_anon_path"] = "auto_js";
        }
        if (values.contains("hashes")) {
            QStringList list;
            const auto encodedHashes = values["hashes"].split('+').mid(0,6);
            for (const auto &hash : encodedHashes) list.append(QUrl::fromPercentEncoding(hash.toUtf8()));
            if (list.size() < 1) return false;
            list = NormalizeHashes(list);
            if (list.size() < 1 || list.size() != encodedHashes.size()) return false;
            QSet<QString> seen;
            for (const auto &hash : list) {
                if (hash.size() < 16 || hash.contains(QRegularExpression("\\s")) || seen.contains(hash)) return false;
                seen.insert(hash);
            }
            object["hashes"] = QJsonArray::fromStringList(list);
        }
    } else {
        object["server"] = url.host(); object["server_port"] = url.port();
        object["password"] = url.userInfo(QUrl::FullyDecoded);
    }
    const auto host = object["server"].toString(), secret = object["password"].toString();
    if (host.isEmpty() || secret.isEmpty() || (host+secret).contains(QRegularExpression("\\s"))) return false;
    return ParseFromJson(object);
}
QJsonObject csqtt::ExportToJson() {
    return {{"type","csqtt"},{"tag",name},{"server",server},{"server_port",server_port},
        {"password",password},{"hashes",QJsonArray::fromStringList(hashes)},{"workers",workers},
        {"turn_tcp",turnTCP},{"obfs",obfs},{"vk_anon_path",vkAnonPath},{"captcha_mode",captchaMode},{"device_id",deviceID},
        {"manual_hashes_override",manualRelayHashes},{"hash_source",hashSource},{"auto_hash_count",autoHashCount},{"vk_access_token",vkAccessToken},
        {"turn_transport",turnTransport},{"srtp_mode",srtpMode},{"fingerprint",fingerprint},
        {"client_ids",clientIds},{"custom_turn_ips",customTurnIps},{"csqtt_version",version}};
}
QJsonObject csqtt::ExportIdentity() { auto object = ExportToJson(); object.remove("tag"); object.remove("device_id"); object.remove("hashes"); object.remove("workers"); object.remove("manual_hashes_override"); return object; }
QString csqtt::ExportToLink() {
    const auto encode = [](const QString &value) { return QString::fromLatin1(QUrl::toPercentEncoding(value)); };
    QString result = "csqtt://connect?v=2&host="+encode(server)+"&peer="+QString::number(server_port)+"&password="+encode(password);
    QStringList encoded; if (hashSource != "auto_js") for (const auto &hash : hashes) encoded.append(encode(hash));
    if (!encoded.isEmpty()) result += "&hashes="+encoded.join('+');
    else if (hashSource == "auto_js" && !vkAccessToken.isEmpty()) result += "&token=" + encode(vkAccessToken);
    if (!name.isEmpty()) result += "&name="+encode(name);
    return csqttSharePayload(result.mid(8), true);
}
QString csqtt::Prepare() {
    hashes = NormalizeHashes(hashes);
    QStringList effectiveHashes = hashes;
    QString effectiveToken = vkAccessToken;
    if (hashSource == "auto_js" && effectiveToken.isEmpty()) {
        QString error; effectiveToken = RelayAccountToken(&error);
        if (!error.isEmpty()) return error;
    }
    if (hashSource == "auto") {
        // The calls come from the qWDTT binding; the helper joins them without a VK login.
        QString error; effectiveHashes = NormalizeHashes(RelayAccountHashes(autoHashCount, &error));
        if (!error.isEmpty()) return error;
    }
    workers = NormalizeWorkers(workers,effectiveHashes.size());
    if (server.isEmpty() || server_port < 1 || server_port > 65535) return QObject::tr("CSQTT: invalid server address or port");
    if (password.isEmpty() || (password+deviceID).contains(QRegularExpression("[|\\r\\n]"))) return QObject::tr("CSQTT: invalid connection credentials");
    if ((hashSource != "auto_js" && effectiveHashes.isEmpty()) || effectiveHashes.size() > 6) return QObject::tr("CSQTT: enter one to six VK call hashes");
    QSet<QString> seen;
    for (const auto &hash : effectiveHashes) {
        if (hash.size() < 16 || hash.contains(QRegularExpression("[,|\\s]")) || seen.contains(hash)) return QObject::tr("CSQTT: invalid or duplicate VK hash");
        seen.insert(hash);
    }
    if (deviceID.isEmpty()) deviceID = RelayDeviceID();
    QString helper = "qwdtt", transport = "csqtt-transport";
#ifdef Q_OS_WIN
    helper += ".exe"; transport += ".exe";
#endif
    const QDir directory(QCoreApplication::applicationDirPath());
    extraCorePath = directory.absoluteFilePath(helper);
    if (!QFileInfo(extraCorePath).isExecutable() || !QFileInfo(directory.absoluteFilePath(transport)).isExecutable()) return QObject::tr("CSQTT: bundled transport is missing");
    QTcpServer tcp; QUdpSocket udp;
    if (!tcp.listen(QHostAddress::LocalHost,0) || !udp.bind(QHostAddress::LocalHost,0)) return QObject::tr("CSQTT: cannot allocate loopback ports");
    socksAddress = "127.0.0.1"; socksPort = tcp.serverPort(); bridgeAuth = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto endpoint = [](const QString &host, int port) { return (host.contains(':') ? "["+host+"]" : host)+":"+QString::number(port); };
    auto config = ExportToJson();
    config["hashes"] = QJsonArray::fromStringList(effectiveHashes);
    config["hash_source"] = hashSource == "auto_js" ? "auto_js" : "manual";
    config["vk_access_token"] = effectiveToken;
    config.remove("auto_hash_count");
    config["backend"] = "csqtt"; config["peer"] = endpoint(server,server_port);
    config["protocol_variant"] = version == "2.1.9" ? "compat" : "legacy";
    config["listen"] = endpoint("127.0.0.1",udp.localPort()); config["socks"] = endpoint(socksAddress,socksPort);
    config["socks_user"] = bridgeAuth; config["socks_pass"] = bridgeAuth;
    extraCoreConf = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact)); extraCoreArgs = "-csqtt-config %s";
    return {};
}
BuildResult csqtt::Build() {
    if (socksPort == 0 || bridgeAuth.isEmpty()) return {{},"CSQTT bridge has not been prepared"};
    return {QJsonObject{{"type","socks"},{"server",socksAddress},{"server_port",socksPort},{"version","5"},{"username",bridgeAuth},{"password",bridgeAuth}}, {}};
}
}
