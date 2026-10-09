#include "include/configs/outbounds/qwdtt.h"
#include "include/configs/common/utils.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
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
    if (m.isEmpty() || m == "raw" || m == "rawtun") return "raw";
    return m == "wg" || m == "vpn" || m == "socks" ? "wg" : m;
}
}

namespace Configs {
QStringList qwdtt::NormalizeHashes(const QStringList &input) {
    QStringList out;
    for (const auto &item : input) {
        for (auto h : item.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts)) {
            h = h.trimmed();
            if (h.startsWith("https://") || h.startsWith("http://")) {
                QUrl u(h);
                h = u.path().section('/', -1);
            }
            if (!h.isEmpty() && !out.contains(h)) out.append(h);
        }
    }
    return out;
}

bool qwdtt::ParseFromJson(const QJsonObject &o) {
    const auto type = o["type"].toString();
    if (!type.isEmpty() && type != "qwdtt" && type != "wdtt") return false;
    outbound::ParseFromJson(o);
    if (o.contains("peer") && !setPeer(*this, o["peer"].toString())) return false;
    if (o.contains("name")) name = o["name"].toString();
    password = o.contains("password") ? o["password"].toString() : o["pass"].toString();
    mode = normalizedMode(o["mode"].toString().toLower());
    rawPort = o["raw_port"].toInt(56003);
    workers = o["workers"].toInt(o["workersPerHash"].toInt(9));
    turnTCP = o["turn_tcp"].toBool(false);
    obfs = o["obfs"].toString("audio");
    goDNS = o["go_dns"].toString("yandex");
    if (!o["device_id"].toString().isEmpty()) deviceID = o["device_id"].toString();
    const auto h = o.contains("hashes") ? o["hashes"] : o["vkHashes"];
    QStringList values;
    if (h.isArray()) { for (const auto &v : h.toArray()) values.append(v.toString()); }
    else values.append(h.toString());
    hashes = NormalizeHashes(values);
    // Empty hashes are editable after importing Android profiles using global hashes.
    return !server.isEmpty() && validPort(server_port) && validPort(rawPort) && hashes.size() <= 4 && (mode == "raw" || mode == "wg");
}

bool qwdtt::ParseFromLink(const QString &link) {
    QString value = link.trimmed();
    if (value.startsWith("qwdtt:config", Qt::CaseInsensitive)) value.replace(0, 12, "qwdtt://config");
    if (value.startsWith("qwdtt://", Qt::CaseInsensitive)) {
        const QUrl u(value);
        if (!u.isValid() || u.host().compare("config", Qt::CaseInsensitive) != 0) return false;
        const QUrlQuery q(u);
        QJsonObject o{{"type", "qwdtt"}};
        for (const auto &key : {"peer", "name", "pass", "password", "hashes", "mode", "obfs", "go_dns", "device_id"}) {
            if (q.hasQueryItem(key)) o[key] = formDecodedQueryValue(q, key);
        }
        for (const auto &key : {"raw_port", "workers"}) {
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
    if (!value.startsWith("wdtt://", Qt::CaseInsensitive)) return false;
    value.remove(0, 7);
    const int fragment = value.indexOf('#');
    QString label;
    if (fragment >= 0) { label = QUrl::fromPercentEncoding(value.mid(fragment + 1).toUtf8()); value.truncate(fragment); }
    const auto parts = value.split(':');
    if (parts.size() < 5 || parts.size() > 6) return false;
    bool ok;
    const int p = parts[1].toInt(&ok);
    if (!ok || !validPort(p)) return false;
    QJsonObject o{{"type", "qwdtt"}, {"peer", peerString(parts[0], p)},
                  {"name", label}, {"password", QUrl::fromPercentEncoding(parts[4].toUtf8())}};
    if (parts.size() == 6) o["hashes"] = QUrl::fromPercentEncoding(parts[5].toUtf8());
    return ParseFromJson(o);
}

QJsonObject qwdtt::ExportToJson() {
    return {{"type", "qwdtt"}, {"tag", name}, {"server", server}, {"server_port", server_port},
            {"password", password}, {"hashes", QJsonArray::fromStringList(hashes)}, {"mode", mode},
            {"raw_port", rawPort}, {"workers", workers}, {"turn_tcp", turnTCP},
            {"obfs", obfs}, {"go_dns", goDNS}, {"device_id", deviceID}};
}
QJsonObject qwdtt::ExportIdentity() { auto o = ExportToJson(); o.remove("tag"); o.remove("device_id"); return o; }
QString qwdtt::ExportToLink() {
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
    addText("go_dns", goDNS);
    addText("device_id", deviceID);
    if (!name.isEmpty()) addText("name", name);
    u.setQuery(q);
    return u.toString(QUrl::FullyEncoded);
}

QString qwdtt::Prepare() {
    hashes = NormalizeHashes(hashes);
    if (server.isEmpty() || !validPort(server_port) || !validPort(rawPort)) return QObject::tr("qWDTT: invalid server address or port");
    if (password.isEmpty() || password.contains('|') || password.contains('\n') || password.contains('\r')) return QObject::tr("qWDTT: a valid connection password is required");
    if (hashes.isEmpty() || hashes.size() > 4) return QObject::tr("qWDTT: enter one to four VK call hashes");
    if (workers < 9 || workers > 108 || workers % 9 != 0) return QObject::tr("qWDTT: workers must be a multiple of 9 (9–108)");
    if (mode != "raw" && mode != "wg") return QObject::tr("qWDTT: invalid tunnel mode");
    if (obfs != "audio" && obfs != "video") return QObject::tr("qWDTT: invalid obfuscation mode");
    if (deviceID.isEmpty()) deviceID = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
                     {"password", password}, {"hashes", QJsonArray::fromStringList(hashes)},
                     {"mode", mode}, {"workers", workers}, {"device_id", deviceID},
                     {"turn_tcp", turnTCP}, {"obfs", obfs}, {"go_dns", goDNS},
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
}
