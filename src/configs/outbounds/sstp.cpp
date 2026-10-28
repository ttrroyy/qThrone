#include "include/configs/outbounds/sstp.h"
#include "include/global/Utils.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QUrlQuery>
#include <QUuid>
namespace Configs {
QString sstp::Validate() const {
    const QUrl endpoint("https://" + (server.contains(':') ? "[" + server + "]" : server));
    if (server.isEmpty() || !endpoint.isValid() || endpoint.host().isEmpty() || !endpoint.path().isEmpty() ||
        !endpoint.userInfo().isEmpty() || endpoint.hasQuery() || endpoint.hasFragment() || server_port < 1 || server_port > 65535)
        return QStringLiteral("SSTP: укажите корректный адрес и порт сервера");
    if (username.trimmed().isEmpty() || password.isEmpty()) return QStringLiteral("SSTP: укажите логин и пароль");
    for (const auto ch : username) if (ch.category() == QChar::Other_Control) return QStringLiteral("SSTP: некорректный логин");
    if (!sni.isEmpty()) {
        const QUrl host("https://" + (sni.contains(':') ? "[" + sni + "]" : sni));
        if (!host.isValid() || host.host().isEmpty() || host.path().size() || !host.userInfo().isEmpty() || host.hasQuery() || host.hasFragment())
            return QStringLiteral("SSTP: SNI должен содержать только имя сервера");
    }
    return {};
}
bool sstp::ParseFromJson(const QJsonObject &o) {
    if (o["type"] != "sstp") return false;
    outbound::ParseFromJson(o);
    name = o["name"].toString(o["tag"].toString());
    auto address = o["server"].toString().trimmed();
    QHostAddress ip(address);
    const auto url = QUrl(address.contains("://") ? address : "sstp://" + (ip.protocol() == QAbstractSocket::IPv6Protocol ? "[" + address + "]" : address));
    if (!url.isValid() || !url.userInfo().isEmpty()) return false;
    server = url.host();
    server_port = o.contains("server_port") ? o["server_port"].toInt() : url.port(443);
    username = o["username"].toString().trimmed(); password = o["password"].toString();
    sni = o["sni"].toString().trimmed(); allowInsecure = o["allow_insecure"].toBool();
    useServerDNS = o["use_server_dns"].toBool();
    return Validate().isEmpty();
}
bool sstp::ParseFromLink(const QString &link) {
    const QUrl url(link.trimmed());
    if (!url.isValid() || url.scheme() != "sstp" || (url.path() != "" && url.path() != "/")) return false;
    const QUrlQuery query(url);
    for (const auto *key : {"sni", "insecure", "use_server_dns"}) if (query.allQueryItemValues(key).size() > 1) return false;
    name = url.fragment(QUrl::FullyDecoded); server = url.host(); server_port = url.port(443);
    username = url.userName(QUrl::FullyDecoded).trimmed(); password = url.password(QUrl::FullyDecoded);
    sni = query.queryItemValue("sni", QUrl::FullyDecoded).trimmed();
    const auto insecure = query.queryItemValue("insecure");
    allowInsecure = insecure == "1" || insecure.compare("true", Qt::CaseInsensitive) == 0;
    const auto serverDns = query.queryItemValue("use_server_dns");
    useServerDNS = serverDns == "1" || serverDns.compare("true", Qt::CaseInsensitive) == 0;
    return Validate().isEmpty();
}
QJsonObject sstp::ExportToJson() {
    return {{"type", "sstp"}, {"name", name}, {"server", server}, {"server_port", server_port},
        {"username", username}, {"password", password}, {"sni", sni}, {"allow_insecure", allowInsecure}, {"use_server_dns", useServerDNS}};
}
QJsonObject sstp::ExportIdentity() { auto o = ExportToJson(); o.remove("name"); return o; }
QString sstp::ExportToLink() {
    QUrl url; url.setScheme("sstp"); url.setHost(server); if (server_port != 443) url.setPort(server_port);
    url.setUserName(username); url.setPassword(password); url.setFragment(name);
    QUrlQuery query; if (!sni.isEmpty()) query.addQueryItem("sni", sni); if (allowInsecure) query.addQueryItem("insecure", "1");
    if (useServerDNS) query.addQueryItem("use_server_dns", "1");
    url.setQuery(query); return url.toString(QUrl::FullyEncoded).replace('+', "%2B");
}
QString sstp::Prepare() {
    if (const auto error = Validate(); !error.isEmpty()) return error;
    QString executable = "sstp-client";
#ifdef Q_OS_WIN
    executable += ".exe";
#endif
    extraCorePath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(executable);
    if (!QFileInfo(extraCorePath).isExecutable()) return QStringLiteral("SSTP: клиент не входит в эту сборку");
    if (socksPort == 0) { const auto ports = MkManyPorts(1); if (ports.isEmpty()) return QStringLiteral("SSTP: не удалось открыть локальный порт"); socksPort = ports.first(); }
    bridgeAuth = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto config = ExportToJson(); config.remove("name"); config["backend"] = "sstp";
    config["socks"] = "127.0.0.1:" + QString::number(socksPort); config["socks_user"] = bridgeAuth; config["socks_pass"] = bridgeAuth;
    config["peer"] = (server.contains(':') ? "[" + server + "]" : server) + ":" + QString::number(server_port);
    config["device_id"] = QString::fromLatin1(QCryptographicHash::hash((username + QChar(0) + password).toUtf8(), QCryptographicHash::Sha256).toHex());
    extraCoreConf = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
    extraCoreArgs = "--qthrone-config %s"; noLogs = true; return {};
}
BuildResult sstp::Build() {
    if (!socksPort || bridgeAuth.isEmpty()) return {{}, "SSTP bridge has not been prepared"};
    return {QJsonObject{{"type", "socks"}, {"server", socksAddress}, {"server_port", socksPort}, {"version", "5"}, {"username", bridgeAuth}, {"password", bridgeAuth}}, {}};
}
}
