#include "include/configs/outbounds/dnstunnel.h"
#include "include/global/Utils.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

namespace Configs {
namespace {
// The engines that take a domain and key of their own and run their own client, not VayDNS's.
bool Keyed(const QString &kind) { return kind == "masterdns" || kind == "cottendns" || kind == "soar"; }

const QString dnstPrefix = QStringLiteral("dnst://");

// SlipNet's tunnel types that are DNS tunnels this client runs, by its export name.
const QMap<QString, QPair<QString, QString>> &dnsSlipNetTypes() {
    static const QMap<QString, QPair<QString, QString>> types{
        {"dnstt", {"dnstt", "socks"}}, {"dnstt_ssh", {"dnstt", "ssh"}},
        {"vaydns", {"vaydns", "socks"}}, {"vaydns_ssh", {"vaydns", "ssh"}},
        {"ss", {"slipstream", "socks"}}, {"slipstream_ssh", {"slipstream", "ssh"}}};
    return types;
}

// Base64 as the apps share it: standard or URL-safe, with or without padding.
QByteArray dnsDecodeBase64(QString text) {
    text = text.trimmed();
    while (text.endsWith('=')) text.chop(1);
    for (const auto options : {QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors,
                               QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors}) {
        const auto decoded = QByteArray::fromBase64Encoding(text.toLatin1(), options);
        if (decoded && !decoded.decoded.isEmpty()) return decoded.decoded;
    }
    return {};
}

QString dnsHostPort(const QString &host, int port) {
    return (host.contains(':') ? "[" + host + "]" : host) + ":" + QString::number(port);
}

QString dnsEncodeAll(const QString &value) { return QString::fromLatin1(QUrl::toPercentEncoding(value)); }

bool dnsHasLineBreak(const QString &value) { return value.contains('\n') || value.contains('\r'); }
}

const QStringList &dnstunnel::Kinds() {
    static const QStringList kinds{"dnstt", "vaydns", "slipstream", "masterdns", "cottendns", "soar"};
    return kinds;
}

const QStringList &dnstunnel::RecordTypes() {
    static const QStringList types{"txt", "cname", "a", "aaaa", "mx", "ns", "srv", "null", "caa"};
    return types;
}

QString dnstunnel::Title(const QString &kind) {
    if (kind == "vaydns") return "VayDNS";
    if (kind == "slipstream") return "SlipStream";
    if (kind == "masterdns") return "MasterDNS";
    if (kind == "cottendns") return "CottenDNS";
    if (kind == "soar") return "Soar";
    return "DNSTT";
}

SecurityInfo dnstunnel::GetSecurity() {
    const auto &d = data;
    if (kind == "slipstream") return {"QUIC TLS", "DNS", SecurityLevel::Secure};
    if (kind == "masterdns" || kind == "cottendns") return {d["encryption"].toInt() == 0 ? QString() : Title(kind), "DNS",
                                     d["encryption"].toInt() == 0 ? SecurityLevel::None : SecurityLevel::Secure};
    if (kind == "soar") return {"X25519 ChaCha20", "DNS", SecurityLevel::Secure};
    return {"Noise", "DNS", SecurityLevel::Secure};
}

void dnstunnel::StripCredentials() {
    for (const auto *key : {"pubkey", "mkey", "cert"}) data.remove(QString::fromLatin1(key));
    data.remove("backend");
}

bool dnstunnel::ParseFromJson(const QJsonObject &o) {
    const auto type = o["type"].toString();
    if (!Kinds().contains(type)) return false;
    kind = type;
    data = o;
    for (const auto *key : {"type", "tag", "name"}) data.remove(QString::fromLatin1(key));
    name = o["name"].toString(o["tag"].toString());
    auto domain = data["domain"].toString().trimmed();
    while (domain.endsWith('.')) domain.chop(1);
    data["domain"] = domain;
    if (!Keyed(kind) && !data.contains("backend")) data["backend"] = QJsonObject{{"type", "socks"}};
    if (Keyed(kind)) data.remove("backend");
    server = domain; server_port = 0;
    return Validate().isEmpty();
}

QString dnstunnel::Validate() const {
    const auto domain = data["domain"].toString();
    static const QRegularExpression hex64("^[0-9a-fA-F]{64}$");
    if (domain.isEmpty() || domain.size() > 253 || domain.contains(QRegularExpression("[\\s/]"))) return QStringLiteral("%1: укажите домен туннеля").arg(Title(kind));
    if ((kind == "dnstt" || kind == "vaydns") && !hex64.match(data["pubkey"].toString()).hasMatch())
        return QStringLiteral("%1: публичный ключ сервера — 64 шестнадцатеричных символа").arg(Title(kind));
    if (kind == "masterdns" || kind == "cottendns") {
        const int encryption = data["encryption"].toInt(-1);
        if (data["mkey"].toString().trimmed().isEmpty() || encryption < 0 || encryption > 5) return QStringLiteral("%1: укажите ключ и метод шифрования").arg(Title(kind));
    }
    if (kind == "soar") {
        static const QRegularExpression label("^[a-z0-9]([a-z0-9-]*[a-z0-9])?$");
        for (const auto &part : domain.toLower().split('.')) {
            if (domain.size() > 45 || !domain.contains('.') || part.size() > 63 || !label.match(part).hasMatch())
                return QStringLiteral("Soar: домен туннеля один, не длиннее 45 символов");
        }
        if (QByteArray::fromBase64(data["pubkey"].toString().trimmed().toLatin1()).size() != 32)
            return QStringLiteral("Soar: публичный ключ сервера Ed25519 в base64 (32 байта)");
    }
    if (kind == "vaydns" && !data["record_type"].toString().isEmpty() && !RecordTypes().contains(data["record_type"].toString()))
        return QStringLiteral("VayDNS: неизвестный тип DNS-записи");
    if (!Keyed(kind)) {
        const auto backend = data["backend"].toObject();
        const auto type = backend["type"].toString();
        if (type == "ssh") {
            if (backend["user"].toString().isEmpty() || (backend["password"].toString().isEmpty() && backend["pk"].toString().isEmpty()))
                return QStringLiteral("%1: для SSH нужны логин и пароль или ключ").arg(Title(kind));
        } else if (type == "shadowsocks") {
            if (backend["password"].toString().isEmpty() || backend["method"].toString().isEmpty())
                return QStringLiteral("%1: для Shadowsocks нужны пароль и метод").arg(Title(kind));
        } else if (type != "socks") {
            return QStringLiteral("%1: неизвестный сервис на сервере").arg(Title(kind));
        }
    }
    for (const auto &resolver : data["resolvers"].toArray()) {
        if (dnsHasLineBreak(resolver.toString())) return QStringLiteral("%1: неверный резолвер").arg(Title(kind));
    }
    if (kind == "slipstream" || Keyed(kind)) {
        for (const auto &resolver : data["resolvers"].toArray()) {
            const auto value = resolver.toString().trimmed();
            if (value.contains("://") && !value.startsWith("udp://")) return QStringLiteral("%1: резолверы — только UDP-адреса").arg(Title(kind));
        }
    }
    return {};
}

bool dnstunnel::ParseFromLink(const QString &link) {
    const auto text = link.trimmed();
    if (text.startsWith(dnstPrefix)) return parseDnst(text.mid(dnstPrefix.size()));
    if (text.startsWith("slipnet://")) return parseSlipNet(text.mid(10));
    for (const auto *scheme : {"masterdns", "stormdns", "cottendns", "soar"}) {
        const auto prefix = QString::fromLatin1(scheme) + "://";
        if (text.startsWith(prefix, Qt::CaseInsensitive)) return parseWhiteDns(text.mid(prefix.size()), QString::fromLatin1(scheme));
    }
    return false;
}

// dnst:// in either form: the URL with the domain as its host, or base64url JSON (spec 2.2: no '/' in it).
bool dnstunnel::parseDnst(const QString &body) {
    const auto content = body.section('#', 0, 0);
    if (!content.contains('/')) {
        const auto json = QJsonDocument::fromJson(dnsDecodeBase64(content)).object();
        if (json.isEmpty()) return false;
        const auto transport = json["transport"].toObject();
        QJsonObject o{{"type", transport["type"].toString()}, {"name", json["tag"].toString()}};
        for (auto it = transport.begin(); it != transport.end(); ++it)
            if (it.key() != "type") o[it.key()] = it.value();
        if (!json["backend"].toObject().isEmpty()) o["backend"] = json["backend"].toObject();
        return ParseFromJson(o);
    }
    const QUrl url(dnstPrefix + body);
    const auto parts = url.path().split('/', Qt::SkipEmptyParts);
    if (!url.isValid() || url.host().isEmpty() || parts.size() < 2) return false;
    const QUrlQuery q(url);
    const auto value = [&q](const QString &key) { return q.queryItemValue(key, QUrl::FullyDecoded); };
    QJsonObject o{{"type", parts[0]}, {"domain", url.host()}, {"name", url.fragment(QUrl::FullyDecoded)}};
    for (const auto *key : {"pubkey", "cert", "record-type", "idle-timeout", "keepalive", "mkey"}) {
        const QString k = QString::fromLatin1(key);
        if (q.hasQueryItem(k)) o[QString(k).replace('-', '_')] = value(k);
    }
    if (q.hasQueryItem("dnstt-compat")) o["dnstt_compat"] = value("dnstt-compat") == "1" || value("dnstt-compat") == "true";
    for (const auto *key : {"clientid-size", "encryption"}) {
        const QString k = QString::fromLatin1(key);
        if (!q.hasQueryItem(k)) continue;
        bool ok = false;
        const int number = value(k).toInt(&ok);
        if (!ok) return false;
        o[QString(k).replace('-', '_')] = number;
    }
    QJsonObject backend{{"type", parts[1]}};
    for (const auto *key : {"user", "password", "pk", "method"}) {
        const QString k = QString::fromLatin1(key);
        if (q.hasQueryItem(k)) backend[k] = value(k);
    }
    o["backend"] = backend;
    return ParseFromJson(o);
}

// SlipNet's export: base64 of pipe-separated fields; new versions only append, so positions hold.
bool dnstunnel::parseSlipNet(const QString &body) {
    const auto f = QString::fromUtf8(dnsDecodeBase64(body.section('#', 0, 0))).split('|');
    if (f.size() < 11 || !dnsSlipNetTypes().contains(f[1])) return false;
    const auto [type, backendType] = dnsSlipNetTypes().value(f[1]);
    const auto field = [&f](int i) { return i < f.size() ? f[i] : QString(); };
    const auto b64 = [&](int i) { return field(i).isEmpty() ? QString() : QString::fromUtf8(dnsDecodeBase64(field(i))); };
    QJsonArray resolvers;
    bool authoritative = false;
    for (const auto &item : field(4).split(',')) authoritative = authoritative || item.section(':', 2, 2) == "1";
    if (field(22) == "doh") {
        if (field(21).startsWith("https://")) resolvers.append(field(21));
    } else {
        for (const auto &item : (field(4).isEmpty() ? field(37) : field(4)).split(',')) {
            const auto parts = item.split(':');
            if (parts.size() < 2 || parts[0].trimmed().isEmpty()) continue;
            bool ok = false;
            int port = parts[1].toInt(&ok);
            if (!ok) port = 53;
            const auto address = dnsHostPort(parts[0], port);
            resolvers.append(field(22) == "dot" ? "tls://" + address : address);
        }
    }
    QJsonObject o{{"type", type}, {"name", field(2)}, {"domain", field(3)}, {"resolvers", resolvers}};
    if (type != "slipstream") o["pubkey"] = field(11);
    if (type == "slipstream") {
        o["authoritative"] = authoritative;
        bool ok = false;
        const int keepAlive = field(6).toInt(&ok);
        if (ok) o["keep_alive_interval"] = keepAlive;
        if (field(7) == "bbr" || field(7) == "dcubic") o["congestion_control"] = field(7);
    }
    if (type == "vaydns") {
        o["dnstt_compat"] = field(41) == "1";
        if (RecordTypes().contains(field(42))) o["record_type"] = field(42);
        bool ok = false;
        const int size = field(49).toInt(&ok);
        if (ok && size > 0) o["clientid_size"] = size;
    }
    QJsonObject backend{{"type", backendType}};
    if (backendType == "ssh") {
        backend["user"] = field(15);
        backend["password"] = field(16);
        if (const auto key = b64(24); !key.trimmed().isEmpty()) backend["pk"] = key;
    } else {
        if (!field(12).isEmpty()) backend["user"] = field(12);
        if (!field(13).isEmpty()) backend["password"] = field(13);
    }
    o["backend"] = backend;
    return ParseFromJson(o);
}

// The whitedns.profile link of the Iranian DNS tunnel apps: base64 JSON whose scheme names the engine.
// StormDNS speaks MasterDnsVPN's protocol and runs on its client; CottenDNS and Soar have their own.
bool dnstunnel::parseWhiteDns(const QString &body, const QString &scheme) {
    return ParseWhiteDnsProfile(QJsonDocument::fromJson(dnsDecodeBase64(body.section('#', 0, 0).section('?', 0, 0))).object(), scheme);
}

bool dnstunnel::ParseWhiteDnsProfile(const QJsonObject &json, QString scheme) {
    if (json["schema"].toString() != "whitedns.profile") return false;
    if (scheme.isEmpty() || scheme == "stormdns" || scheme == "masterdns") scheme = json["import_type"].toString(scheme).toLower();
    const auto profile = json["profile"].toObject();
    const auto serverObject = profile["server"].toObject();
    QStringList domains;
    for (const auto &value : serverObject["domains"].toArray()) domains << value.toString().trimmed();
    domains.removeAll(QString());
    if (domains.isEmpty()) domains << serverObject["domain"].toString().trimmed();
    if (scheme == "soar") {
        QJsonArray resolvers;
        for (const auto &value : profile["resolvers"].toArray()) resolvers.append(value.toString());
        QJsonObject o{{"type", "soar"}, {"name", profile["name"].toString()}, {"domain", domains.first()},
                      {"pubkey", serverObject["public_key"].toString()}, {"resolvers", resolvers}};
        if (profile["soarSettings"].isObject()) o["soar"] = profile["soarSettings"].toObject();
        return ParseFromJson(o);
    }
    auto key = serverObject["encryption_key"].toString();
    if (key.isEmpty()) key = serverObject["encryptionKey"].toString();
    const auto method = serverObject.contains("encryption_method") ? serverObject["encryption_method"].toInt(1) : serverObject["encryptionMethod"].toInt(1);
    return ParseFromJson(QJsonObject{{"type", scheme == "cottendns" ? "cottendns" : "masterdns"}, {"name", profile["name"].toString()},
                                     {"domain", domains.join(',')}, {"mkey", key}, {"encryption", method}});
}

// CottenDNS and Soar profiles travel as the links their own apps import.
QString dnstunnel::exportWhiteDns() const {
    QJsonObject serverObject;
    const auto domains = data["domain"].toString().split(',', Qt::SkipEmptyParts);
    serverObject["domain"] = domains.value(0);
    QJsonObject profile{{"name", name.isEmpty() ? domains.value(0) : name}};
    if (kind == "soar") {
        serverObject["public_key"] = data["pubkey"].toString();
        if (data["soar"].isObject()) profile["soarSettings"] = data["soar"].toObject();
        if (!data["resolvers"].toArray().isEmpty()) profile["resolvers"] = data["resolvers"].toArray();
    } else {
        serverObject["domains"] = QJsonArray::fromStringList(domains);
        serverObject["encryption_key"] = data["mkey"].toString();
        serverObject["encryption_method"] = data["encryption"].toInt();
    }
    profile["server"] = serverObject;
    const QJsonObject json{{"schema", "whitedns.profile"}, {"version", 1}, {"import_type", kind}, {"profile", profile}};
    return kind + "://" + QString::fromLatin1(QJsonDocument(json).toJson(QJsonDocument::Compact)
                                              .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QJsonObject dnstunnel::ExportToJson() {
    auto o = data;
    o["type"] = kind;
    if (!name.isEmpty()) o["name"] = name;
    return o;
}

QJsonObject dnstunnel::ExportIdentity() { auto o = ExportToJson(); o.remove("name"); return o; }

// dnst:// (spec 2): the readable form, or base64url JSON when a certificate or key would not fit in a URL.
QString dnstunnel::ExportToLink() {
    const auto &d = data;
    if (!Validate().isEmpty()) return {};
    if (kind == "cottendns" || kind == "soar") return exportWhiteDns();
    const auto backend = d["backend"].toObject();
    if (!d["cert"].toString().isEmpty() || !backend["pk"].toString().isEmpty()) {
        QJsonObject transport{{"type", kind}};
        for (auto it = data.begin(); it != data.end(); ++it)
            if (it.key() != "backend" && it.key() != "resolvers") transport[it.key()] = it.value();
        QJsonObject json{{"transport", transport}, {"backend", kind == "masterdns" ? QJsonObject{{"type", "socks"}} : backend}};
        if (!name.isEmpty()) json["tag"] = name;
        return dnstPrefix + QString::fromLatin1(QJsonDocument(json).toJson(QJsonDocument::Compact)
                                                .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    }
    QStringList query;
    const auto add = [&query](const QString &key, const QString &value) { if (!value.isEmpty()) query << key + "=" + dnsEncodeAll(value); };
    add("pubkey", d["pubkey"].toString());
    add("record-type", d["record_type"].toString());
    if (d["dnstt_compat"].toBool()) add("dnstt-compat", "true");
    if (d["clientid_size"].toInt() > 0) add("clientid-size", QString::number(d["clientid_size"].toInt()));
    add("idle-timeout", d["idle_timeout"].toString());
    add("keepalive", d["keepalive"].toString());
    add("mkey", d["mkey"].toString());
    if (kind == "masterdns") add("encryption", QString::number(d["encryption"].toInt()));
    for (const auto *key : {"user", "password", "method"}) add(QString::fromLatin1(key), backend[QString::fromLatin1(key)].toString());
    const auto path = "/" + kind + "/" + (kind == "masterdns" ? QStringLiteral("socks") : backend["type"].toString());
    return dnstPrefix + d["domain"].toString() + path + (query.isEmpty() ? QString() : "?" + query.join('&')) +
           (name.isEmpty() ? QString() : "#" + dnsEncodeAll(name));
}

QString dnstunnel::Prepare() {
    const auto &d = data;
    if (const auto error = Validate(); !error.isEmpty()) return error;
    QString executable = Keyed(kind) ? kind : QStringLiteral("dnstun");
#ifdef Q_OS_WIN
    executable += ".exe";
#endif
    const QDir directory(QCoreApplication::applicationDirPath());
    extraCorePath = directory.absoluteFilePath(executable);
    if (!QFileInfo(extraCorePath).isExecutable()) return QStringLiteral("%1: встроенный клиент не найден").arg(Title(kind));
    if (kind == "slipstream") {
        QString client = "slipstream-client";
#ifdef Q_OS_WIN
        client += ".exe";
#endif
        if (!QFileInfo(directory.absoluteFilePath(client)).isExecutable()) return QStringLiteral("SlipStream: клиент не входит в эту сборку");
    }
    if (socksPort == 0) { const auto ports = MkManyPorts(1); if (ports.isEmpty()) return QStringLiteral("%1: не удалось открыть локальный порт").arg(Title(kind)); socksPort = ports.first(); }
    bridgeAuth = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto config = data;
    config["type"] = kind;
    // "backend" tells the core which transport this is; the helper reads the server's service here.
    config.remove("backend");
    if (!Keyed(kind)) config["tunnel_backend"] = d["backend"].toObject();
    if (kind == "soar") config["server_public_key"] = d["pubkey"].toString();
    config["backend"] = kind;
    config["socks"] = "127.0.0.1:" + QString::number(socksPort);
    config["socks_user"] = bridgeAuth;
    config["socks_pass"] = bridgeAuth;
    // Session identity for tests and routing: one tunnel per server and key.
    config["peer"] = kind + ":" + d["domain"].toString();
    const auto secret = d["pubkey"].toString() + d["mkey"].toString() + d["cert"].toString();
    config["device_id"] = QString::fromLatin1(QCryptographicHash::hash(secret.toUtf8(), QCryptographicHash::Sha256).toHex());
    extraCoreConf = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
    extraCoreArgs = "--qthrone-config %s";
    noLogs = true;
    return {};
}

BuildResult dnstunnel::Build() {
    if (socksPort == 0 || bridgeAuth.isEmpty()) return {{}, Title(kind) + " bridge has not been prepared"};
    // The tunnels carry TCP streams to the server's backend only.
    return {QJsonObject{{"type", "socks"}, {"server", socksAddress}, {"server_port", socksPort}, {"version", "5"},
                        {"username", bridgeAuth}, {"password", bridgeAuth}, {"network", "tcp"}}, {}};
}
}
