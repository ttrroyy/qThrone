#include "include/configs/outbounds/freeturn.h"
#include "include/global/Utils.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
namespace Configs {
namespace {
constexpr auto scheme = "freeturn://";
// WireGuard goes over TURN: the original's constant, its server keeps the same value.
constexpr int wireGuardMtu = 1280;
QString hostPort(const QString &host, int port) { return (host.contains(':') ? "[" + host + "]" : host) + ":" + QString::number(port); }
}

QStringList freeturn::linkList() const {
    QStringList result;
    for (const auto &line : QString(links).replace(',', '\n').split('\n', Qt::SkipEmptyParts))
        if (!line.trimmed().isEmpty()) result << line.trimmed();
    return result;
}

QString freeturn::Validate() const {
    const QUrl endpoint("udp://" + hostPort(server, server_port));
    if (server.isEmpty() || !endpoint.isValid() || endpoint.host().isEmpty() || server_port < 1 || server_port > 65535)
        return QStringLiteral("FreeTurn: укажите корректный адрес и порт сервера");
    if (provider != "vk" && provider != "direct") return QStringLiteral("FreeTurn: неизвестный источник потоков");
    if (relayTransport != "tcp" && relayTransport != "udp") return QStringLiteral("FreeTurn: неизвестный транспорт");
    if (tunnelMode != "udp" && tunnelMode != "tcp") return QStringLiteral("FreeTurn: неизвестный режим");
    if (!QStringList{"none", "rtpopus", "rtpopus2", "rtpopus3"}.contains(obfProfile)) return QStringLiteral("FreeTurn: неизвестный профиль обфускации");
    if (obfProfile != "none" && !QRegularExpression("^[0-9a-fA-F]{64}$").match(obfKey).hasMatch()) return QStringLiteral("FreeTurn: ключ обфускации — 64 HEX-символа");
    if (!QStringList{"auto", "plain", "doh"}.contains(dnsMode)) return QStringLiteral("FreeTurn: неизвестный режим DNS");
    if (threads < 0 || threads > 256 || streamsPerCred < 0 || streamsPerCred > 256 || obfTiming < 0) return QStringLiteral("FreeTurn: проверьте число потоков");
    return {};
}

bool freeturn::ParseFromJson(const QJsonObject &o) {
    if (o["type"] != "freeturn") return false;
    outbound::ParseFromJson(o);
    name = o["name"].toString(o["tag"].toString());
    server = o["server"].toString().trimmed();
    server_port = o["server_port"].toInt(56000);
    provider = o["provider"].toString("vk");
    links = o["links"].toString().trimmed();
    threads = o["threads"].toInt(12);
    streamsPerCred = o["streams_per_cred"].toInt(12);
    relayTransport = o["relay_transport"].toString("tcp");
    tunnelMode = o["tunnel_mode"].toString("udp");
    bond = o["bond"].toBool();
    obfProfile = o["obf_profile"].toString("none");
    obfKey = o["obf_key"].toString().trimmed();
    obfTiming = o["obf_timing"].toInt();
    clientId = o["client_id"].toString().trimmed();
    dnsMode = o["dns_mode"].toString("auto");
    dnsServers = o["dns_servers"].toString().trimmed();
    manualCaptcha = o["manual_captcha"].toBool();
    turnHost = o["turn_host"].toString().trimmed();
    wgConfig = o["wg_config"].toString().trimmed();
    kcp = o["kcp"].toObject();
    return Validate().isEmpty();
}

// The original app's share link; the call link may be missing, it belongs to each client.
bool freeturn::ParseFromLink(const QString &link) {
    const auto text = link.trimmed();
    if (!text.startsWith(scheme, Qt::CaseInsensitive)) return false;
    const auto decoded = QByteArray::fromBase64Encoding(text.mid(QString(scheme).size()).toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    if (!decoded) return false;
    const auto payload = QJsonDocument::fromJson(*decoded).object();
    if (payload["v"].toInt() != 1) return false;
    const QUrl peer("udp://" + payload["peer"].toString());
    if (!peer.isValid() || peer.host().isEmpty()) return false;
    QJsonObject o{{"type", "freeturn"}, {"name", payload["name"]}, {"server", peer.host()}, {"server_port", peer.port(56000)},
        {"provider", payload["provider"].toString("vk")}, {"relay_transport", payload["transport"].toString("tcp")}, {"tunnel_mode", payload["mode"].toString("udp")},
        {"bond", payload["bond"]}, {"obf_profile", payload["obf"].toString("none")}, {"obf_key", payload["key"]}, {"obf_timing", payload["timing"]},
        {"client_id", payload["cid"]}, {"dns_mode", payload["dns"].toString("auto")}, {"dns_servers", payload["dnss"]},
        {"manual_captcha", payload["mcap"]}, {"links", payload["vk"]}, {"wg_config", payload["wg"]}};
    if (payload["n"].toInt()) o["threads"] = payload["n"];
    if (payload["spc"].toInt()) o["streams_per_cred"] = payload["spc"];
    if (payload.contains("kcp")) o["kcp"] = payload["kcp"];
    return ParseFromJson(o);
}

QJsonObject freeturn::ExportToJson() {
    QJsonObject o{{"type", "freeturn"}, {"name", name}, {"server", server}, {"server_port", server_port}, {"provider", provider}, {"links", links},
        {"threads", threads}, {"streams_per_cred", streamsPerCred}, {"relay_transport", relayTransport}, {"tunnel_mode", tunnelMode}, {"bond", bond},
        {"obf_profile", obfProfile}, {"obf_key", obfKey}, {"obf_timing", obfTiming}, {"client_id", clientId}, {"dns_mode", dnsMode}, {"dns_servers", dnsServers},
        {"manual_captcha", manualCaptcha}, {"turn_host", turnHost}, {"wg_config", wgConfig}};
    if (!kcp.isEmpty()) o["kcp"] = kcp;
    return o;
}

QJsonObject freeturn::ExportIdentity() { auto o = ExportToJson(); o.remove("name"); o.remove("client_id"); return o; }

QString freeturn::ExportToLink() {
    QJsonObject o{{"v", 1}, {"provider", provider}, {"peer", hostPort(server, server_port)}, {"transport", relayTransport}, {"mode", tunnelMode}};
    if (bond) o["bond"] = true;
    if (obfProfile != "none") { o["obf"] = obfProfile; o["key"] = obfKey; if (obfTiming) o["timing"] = obfTiming; }
    if (threads) o["n"] = threads;
    if (streamsPerCred) o["spc"] = streamsPerCred;
    if (!clientId.isEmpty()) o["cid"] = clientId;
    if (dnsMode != "auto") o["dns"] = dnsMode;
    if (!dnsServers.isEmpty()) o["dnss"] = dnsServers;
    if (manualCaptcha) o["mcap"] = true;
    if (!kcp.isEmpty()) o["kcp"] = kcp;
    if (!name.isEmpty()) o["name"] = name;
    if (!links.isEmpty()) o["vk"] = links;
    if (!wgConfig.isEmpty()) o["wg"] = wgConfig;
    return scheme + QString::fromLatin1(QJsonDocument(o).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString freeturn::Prepare() {
    if (const auto error = Validate(); !error.isEmpty()) return error;
    if (provider != "direct" && linkList().isEmpty()) return QStringLiteral("FreeTurn: добавьте ссылку на звонок VK");
    const bool tcp = tunnelMode == "tcp";
    if (!tcp && !wgConfig.contains("[Interface]", Qt::CaseInsensitive)) return QStringLiteral("FreeTurn: добавьте конфигурацию WireGuard или AmneziaWG");
    QString executable = "freeturn-client";
#ifdef Q_OS_WIN
    executable += ".exe";
#endif
    extraCorePath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(executable);
    if (!QFileInfo(extraCorePath).isExecutable()) return QStringLiteral("FreeTurn: клиент не входит в эту сборку");
    if (socksPort == 0) {
        const auto ports = MkManyPorts(1);
        if (ports.isEmpty()) return QStringLiteral("FreeTurn: не удалось выбрать локальный порт");
        socksPort = ports.first();
    }
    const auto forward = MkManyPorts(1);
    if (forward.isEmpty()) return QStringLiteral("FreeTurn: не удалось выбрать локальный порт");
    bridgeAuth = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonArray linksArray, dnsArray;
    for (const auto &item : linkList()) linksArray.append(item);
    if (linksArray.isEmpty()) linksArray.append(QString());
    for (const auto &item : dnsServers.split(',', Qt::SkipEmptyParts)) dnsArray.append(item.trimmed());
    const bool obf = obfProfile != "none";
    // config.ClientJSON of the core: its decoder refuses unknown fields.
    QJsonObject proxy{{"mode", tunnelMode}, {"listen", "127.0.0.1:" + QString::number(forward.first())}};
    if (tcp && bond) proxy["bond"] = true;
    QJsonObject core{{"peer", hostPort(server, server_port)}, {"clientId", clientId}, {"subUrl", ""}, {"provider", provider}, {"routes", false},
        {"turn", QJsonObject{{"n", provider == "direct" ? 1 : (threads > 0 ? threads : 12)}, {"transport", relayTransport}, {"host", turnHost}, {"port", ""}}},
        {"proxy", proxy},
        {"vk", QJsonObject{{"links", linksArray}, {"streamsPerCred", streamsPerCred > 0 ? streamsPerCred : 12}, {"manualCaptcha", manualCaptcha},
            {"autoCaptcha", true}, {"advancedCaptcha", false}, {"platform", "desktop"}}},
        {"obf", QJsonObject{{"profile", obfProfile}, {"key", obf ? obfKey : QString()}, {"timingMs", obf ? obfTiming : 0}}},
        {"dns", QJsonObject{{"mode", dnsMode}, {"servers", dnsArray}}},
        {"log", QJsonObject{{"debug", false}}},
        {"tunnel", QJsonObject{{"mode", tcp ? "none" : "awg"}, {"config", tcp ? QString() : wgConfig}, {"mtu", wireGuardMtu}}}};
    if (tcp && !kcp.isEmpty()) core["kcp"] = kcp;
    const auto state = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).absoluteFilePath("freeturn");
    QDir().mkpath(state);
    QJsonObject config{{"core", core}, {"state_dir", state}, {"backend", "freeturn"}, {"peer", hostPort(server, server_port)},
        {"device_id", QString::fromLatin1(QCryptographicHash::hash((server + QChar(0) + clientId + QChar(0) + links).toUtf8(), QCryptographicHash::Sha256).toHex())},
        {"socks", "127.0.0.1:" + QString::number(socksPort)}, {"socks_user", bridgeAuth}, {"socks_pass", bridgeAuth}};
    extraCoreConf = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
    extraCoreArgs = "--rawline-config %s"; noLogs = true;
    return {};
}

BuildResult freeturn::Build() {
    if (!socksPort || bridgeAuth.isEmpty()) return {{}, "FreeTurn bridge has not been prepared"};
    QJsonObject object{{"type", "socks"}, {"server", socksAddress}, {"server_port", socksPort}, {"version", "5"}, {"username", bridgeAuth}, {"password", bridgeAuth}};
    // TCP mode forwards streams to the server's backend: nothing else passes this hop.
    if (tunnelMode == "tcp") object["network"] = "tcp";
    return {object, ""};
}
}
