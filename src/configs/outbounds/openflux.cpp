#include "include/configs/outbounds/openflux.h"
#include "include/global/Utils.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QUrl>
#include <QUuid>
#include <zlib.h>

namespace Configs {
namespace {
constexpr int payloadLimit = 16384;
constexpr int minSecretChars = 16;
const QString prefix = QStringLiteral("openflux://v1/");
// Earlier builds stored this when a link had no context; OpenFlux derives the real one.
const QString inventedContext = QStringLiteral("http://openflux");

QByteArray deflatePayload(const QByteArray &input, bool compress) {
    z_stream stream{};
    const int init = compress ? deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY)
                              : inflateInit2(&stream, -MAX_WBITS);
    if (init != Z_OK) return {};
    QByteArray output(payloadLimit + 1, '\0');
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    stream.avail_in = input.size();
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = output.size();
    const int status = compress ? deflate(&stream, Z_FINISH) : inflate(&stream, Z_FINISH);
    const bool valid = status == Z_STREAM_END && stream.total_out <= payloadLimit && stream.avail_in == 0;
    const auto size = stream.total_out;
    if (compress) deflateEnd(&stream); else inflateEnd(&stream);
    if (!valid) return {};
    output.resize(size);
    return output;
}

// share.normalizeBody: what chats, terminals and QR apps do to the base64url part.
QByteArray normalizeBody(const QString &body) {
    QString out;
    for (const auto c : body) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == QChar(0x00a0) || c == QChar(0x200b)) continue;
        out += c == '+' ? QChar('-') : c == '/' ? QChar('_') : c;
    }
    while (out.endsWith('=')) out.chop(1);
    return out.toLatin1();
}

bool hasLineBreak(const QString &value) { return value.contains('\n') || value.contains('\r'); }

bool validDial(const QString &dial) {
    const QUrl address("tcp://" + dial.trimmed());
    return address.isValid() && !address.host().isEmpty() && address.port() >= 1 && address.port() <= 65535
        && address.userInfo().isEmpty() && address.path().isEmpty();
}

// One entry per carrier, so profiles riding the same exit carrier compare equal.
QString carrierIdentity(const QJsonArray &transports) {
    QStringList carriers;
    for (const auto &entry : transports) {
        const auto t = entry.toObject();
        const auto type = t["type"].toString();
        carriers << type + " " + t[type == "direct" ? "dial" : "url"].toString().trimmed();
    }
    carriers.sort();
    return carriers.join('\n');
}
}

const QStringList &openflux::CarrierTypes() {
    static const QStringList types{"yandex", "vyandex", "boards", "mailru", "cupsonline", "avito", "telemost", "direct"};
    return types;
}

// share.Decode: lenient about how the link travelled, strict about what it says.
bool openflux::ParseFromLink(const QString &link) {
    const auto text = link.trimmed();
    if (!text.startsWith(prefix) || text.size() > 65536) return false;
    const auto fragment = text.indexOf('#');
    const auto body = normalizeBody(text.mid(prefix.size(), fragment < 0 ? -1 : fragment - prefix.size()));
    const auto decoded = QByteArray::fromBase64Encoding(body, QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty()) return false;
    const auto payload = deflatePayload(decoded.decoded, false);
    if (payload.isEmpty()) return false;
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    auto object = document.object();
    object["type"] = "openflux";
    if (fragment >= 0) {
        const auto label = QUrl::fromPercentEncoding(text.mid(fragment + 1).toUtf8()).trimmed();
        if (!label.isEmpty()) object["name"] = label;
    }
    return ParseFromJson(object);
}

bool openflux::ParseFromJson(const QJsonObject &o) {
    if (o["type"].toString() != "openflux") return false;
    auto data = o;
    name = o["name"].toString(o["tag"].toString());
    for (const auto *key : {"type", "tag", "name"}) data.remove(QString::fromLatin1(key));
    if (data["context"].toString() == inventedContext) data.remove("context");
    if (data["codec"].toString() == "batched") data.remove("codec");
    // Hand-written JSON may leave the mode out where only a Session can work.
    if (!data.contains("negotiate") && data["mode"].toString().isEmpty()) {
        const auto list = data["transports"].toArray();
        bool direct = false;
        for (const auto &entry : list) direct = direct || entry.toObject()["type"].toString() == "direct";
        if (list.size() > 1 || direct) data["negotiate"] = true;
    }
    secret = data.take("secret").toString();
    context = data.take("context").toString().trimmed();
    codec = data.take("codec").toString();
    mode = data.take("mode").toString();
    negotiate = data.take("negotiate").toBool(false);
    transports = data.take("transports").toArray();
    extra = data;
    server.clear();
    for (const auto &entry : transports) {
        const auto t = entry.toObject();
        if (t["type"].toString() != "direct") continue;
        const QUrl address("tcp://" + t["dial"].toString().trimmed());
        server = address.host(); server_port = address.port(server_port);
        break;
    }
    return Validate().isEmpty();
}

// share.Config.Validate, with the line checks the private configuration needs.
QString openflux::Validate() const {
    if (transports.isEmpty() || transports.size() > 16) return QStringLiteral("OpenFlux: укажите от одного до шестнадцати каналов");
    if (!mode.isEmpty()) {
        const auto type = transports.first().toObject()["type"].toString();
        if (mode != "stream" || transports.size() != 1 || negotiate || !secret.isEmpty() || (type != "cupsonline" && type != "mailru"))
            return QStringLiteral("OpenFlux: неверные параметры потокового режима");
    }
    if (transports.size() > 1 && !negotiate) return QStringLiteral("OpenFlux: несколько каналов работают только в режиме сессии");
    // Characters as Kotlin and Java count them (UTF-16 units), like the other clients.
    if ((negotiate || !secret.isEmpty()) && secret.size() < minSecretChars) return QStringLiteral("OpenFlux: секрет должен содержать не менее 16 символов");
    if (!codec.isEmpty() && codec != "batched" && codec != "legacy") return QStringLiteral("OpenFlux: неизвестный кодек");
    if (hasLineBreak(secret) || hasLineBreak(context)) return QStringLiteral("OpenFlux: неверный секрет или контекст подключения");
    for (const auto &entry : transports) {
        if (!entry.isObject()) return QStringLiteral("OpenFlux: неверные параметры каналов");
        const auto t = entry.toObject();
        const auto type = t["type"].toString();
        if (!CarrierTypes().contains(type)) return QStringLiteral("OpenFlux: неподдерживаемый канал %1").arg(type);
        if (hasLineBreak(t["url"].toString()) || hasLineBreak(t["dial"].toString()) || hasLineBreak(t["name"].toString())) return QStringLiteral("OpenFlux: неверные параметры каналов");
        if (type == "direct") {
            if (!negotiate) return QStringLiteral("OpenFlux: прямое подключение работает только в режиме сессии");
            if (!validDial(t["dial"].toString())) return QStringLiteral("OpenFlux: неверный адрес сервера");
        } else if (t["url"].toString().trimmed().isEmpty()) {
            return QStringLiteral("OpenFlux: укажите ссылку для канала %1").arg(type);
        }
    }
    return {};
}

QJsonObject openflux::ExportToJson() {
    auto o = extra;
    o["type"] = "openflux";
    if (!name.isEmpty()) o["name"] = name;
    if (!secret.isEmpty()) o["secret"] = secret;
    if (!context.isEmpty()) o["context"] = context;
    if (!codec.isEmpty()) o["codec"] = codec;
    if (!mode.isEmpty()) o["mode"] = mode;
    if (negotiate) o["negotiate"] = true;
    o["transports"] = transports;
    return o;
}

QJsonObject openflux::ExportIdentity() { auto o = ExportToJson(); o.remove("name"); return o; }

// share.Make's canonical form, so the link reads the same in every OpenFlux client.
QString openflux::ExportToLink() {
    if (!Validate().isEmpty()) return {};
    auto o = ExportToJson(); o.remove("type");
    if (!name.trimmed().isEmpty()) o["name"] = name.trimmed();
    if (transports.size() == 1) {
        auto only = transports.first().toObject(); only.remove("priority");
        o["transports"] = QJsonArray{only};
    }
    const auto json = QJsonDocument(o).toJson(QJsonDocument::Compact);
    if (json.size() > payloadLimit) return {};
    const auto data = deflatePayload(json, true);
    if (data.isEmpty()) return {};
    return prefix + QString::fromLatin1(data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString openflux::Prepare() {
    if (const auto error = Validate(); !error.isEmpty()) return error;
    QString executable = "openflux";
#ifdef Q_OS_WIN
    executable += ".exe";
#endif
    extraCorePath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(executable);
    if (socksPort == 0) { const auto ports = MkManyPorts(1); if (ports.isEmpty()) return "OpenFlux: не удалось открыть локальный порт"; socksPort = ports.first(); }
    bridgeAuth = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto config = ExportToJson();
    config.remove("name");
    config["backend"] = "openflux";
    config["socks"] = "127.0.0.1:" + QString::number(socksPort);
    config["socks_user"] = bridgeAuth;
    config["socks_pass"] = bridgeAuth;
    // An exit serves one client at a time: profiles riding the same carrier share its session.
    config["peer"] = carrierIdentity(transports);
    config["device_id"] = QString::fromLatin1(QCryptographicHash::hash(secret.toUtf8(), QCryptographicHash::Sha256).toHex());
    extraCoreConf = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
    extraCoreArgs = "--qthrone-config %s";
    noLogs = true;
    return {};
}

BuildResult openflux::Build() {
    if (socksPort == 0 || bridgeAuth.isEmpty()) return {{}, "OpenFlux bridge has not been prepared"};
    return {QJsonObject{{"type", "socks"}, {"server", socksAddress}, {"server_port", socksPort},
                        {"version", "5"}, {"username", bridgeAuth}, {"password", bridgeAuth}}, {}};
}
}
