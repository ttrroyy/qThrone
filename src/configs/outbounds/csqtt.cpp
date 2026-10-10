#include "include/configs/outbounds/csqtt.h"
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

namespace Configs {
QStringList csqtt::NormalizeHashes(const QStringList &input) {
    QStringList result;
    for (auto hash : input) {
        hash = hash.trimmed();
        hash.remove(QRegularExpression("^(?:https?://)?(?:m\\.)?vk\\.(?:com|ru)/call/join/",QRegularExpression::CaseInsensitiveOption));
        hash = hash.section('?',0,0).section('#',0,0);
        while (hash.endsWith('/')) hash.chop(1);
        if (!hash.isEmpty()) result.append(hash);
    }
    return result;
}
bool csqtt::ParseFromJson(const QJsonObject &object) {
    if (object["type"].toString() != "csqtt") return false;
    outbound::ParseFromJson(object);
    password = object["password"].toString();
    hashes.clear();
    for (const auto &hash : object["hashes"].toArray()) hashes.append(hash.toString());
    hashes = NormalizeHashes(hashes.mid(0,6));
    workers = NormalizeWorkers(object["workers"].toInt(18),hashes.size());
    turnTCP = object["turn_tcp"].toBool(false);
    obfs = object["obfs"].toString("audio");
    vkAnonPath = object["vk_anon_path"].toString("vkcalls");
    captchaMode = object["captcha_mode"].toString("auto");
    if (!object["device_id"].toString().isEmpty()) deviceID = object["device_id"].toString();
    return !server.isEmpty() && server_port > 0 && server_port <= 65535 && hashes.size() <= 6
        && (obfs == "audio" || obfs == "video") && (vkAnonPath == "vkcalls" || vkAnonPath == "legacy")
        && (captchaMode == "auto" || captchaMode == "wv" || captchaMode == "rjs");
}
bool csqtt::ParseFromLink(const QString &link) {
    QUrl url(link.trimmed());
    if (!url.isValid() || url.scheme().compare("csqtt",Qt::CaseInsensitive)) return false;
    QJsonObject object{{"type","csqtt"}};
    if (url.hasFragment()) object["tag"] = url.fragment(QUrl::FullyDecoded);
    if (url.host().compare("connect",Qt::CaseInsensitive) == 0) {
        if (!url.userInfo().isEmpty() || url.port() != -1 || !url.path().isEmpty()) return false;
        auto query = url.query(QUrl::FullyEncoded).replace("&amp;","&");
        QMap<QString,QString> values;
        for (auto part : query.split(QRegularExpression("[&;]"))) {
            int index = part.indexOf('='); if (index > 0) values[part.left(index)] = part.mid(index+1);
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
        {"turn_tcp",turnTCP},{"obfs",obfs},{"vk_anon_path",vkAnonPath},{"captcha_mode",captchaMode},{"device_id",deviceID}};
}
QJsonObject csqtt::ExportIdentity() { auto object = ExportToJson(); object.remove("tag"); object.remove("device_id"); return object; }
QString csqtt::ExportToLink() {
    const auto encode = [](const QString &value) { return QString::fromLatin1(QUrl::toPercentEncoding(value)); };
    QString result = "csqtt://connect?v=2&host="+encode(server)+"&peer="+QString::number(server_port)+"&password="+encode(password);
    QStringList encoded; for (const auto &hash : hashes) encoded.append(encode(hash));
    if (!encoded.isEmpty()) result += "&hashes="+encoded.join('+');
    if (!name.isEmpty()) result += "#"+encode(name);
    return result;
}
QString csqtt::Prepare() {
    hashes = NormalizeHashes(hashes); workers = NormalizeWorkers(workers,hashes.size());
    if (server.isEmpty() || server_port < 1 || server_port > 65535) return QObject::tr("CSQTT: invalid server address or port");
    if (password.isEmpty() || (password+deviceID).contains(QRegularExpression("[|\\r\\n]"))) return QObject::tr("CSQTT: invalid connection credentials");
    if (hashes.isEmpty() || hashes.size() > 6) return QObject::tr("CSQTT: enter one to six VK call hashes");
    QSet<QString> seen;
    for (const auto &hash : hashes) {
        if (hash.size() < 16 || hash.contains(QRegularExpression("[,|\\s]")) || seen.contains(hash)) return QObject::tr("CSQTT: invalid or duplicate VK hash");
        seen.insert(hash);
    }
    if (deviceID.isEmpty()) deviceID = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
    config["backend"] = "csqtt"; config["peer"] = endpoint(server,server_port);
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
