#include "include/configs/outbounds/olcrtc.h"
#include "include/global/Utils.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
namespace Configs {
bool olcrtc::ParseFromJson(const QJsonObject &o) {
    name = o["name"].toString(); provider = o["provider"].toString("jitsi"); transport = o["transport"].toString("datachannel");
    room = o["room"].toString(); key = o["key"].toString(); token = o["token"].toString(); channel = o["channel"].toString();
    dns = o["dns"].toString("8.8.8.8:53"); options = o["options"].toObject();
    if (!o["device_id"].toString().isEmpty()) deviceID = o["device_id"].toString();
    const QUrl url(room.startsWith("https://") ? room : "https://" + room);
    server = url.host().isEmpty() ? provider : url.host(); server_port = 443;
    return Validate().isEmpty();
}
bool olcrtc::ParseFromLink(const QString &link) {
    if (!link.startsWith("olcrtc://") || link.size() > 16384) return false;
    const auto text = link.mid(9); const auto question = text.indexOf('?'), at = text.indexOf('@', question + 1), hash = text.lastIndexOf('#');
    if (question < 1 || at <= question || hash <= at) return false;
    const auto dollar = text.indexOf('$', hash + 1);
    QJsonObject o{{"provider", text.left(question)}, {"room", QUrl::fromPercentEncoding(text.mid(at + 1, hash - at - 1).toUtf8())},
        {"key", text.mid(hash + 1, dollar < 0 ? -1 : dollar - hash - 1)}, {"name", dollar < 0 ? QString() : QUrl::fromPercentEncoding(text.mid(dollar + 1).toUtf8())}};
    auto spec = text.mid(question + 1, at - question - 1); const auto bracket = spec.indexOf('<');
    if (bracket >= 0) {
        if (!spec.endsWith('>')) return false;
        QJsonObject params; const QUrlQuery query(spec.mid(bracket + 1, spec.size() - bracket - 2));
        for (const auto &pair : query.queryItems()) {
            if (params.contains(pair.first)) return false;
            if (pair.first == "video-bitrate" || pair.first == "video-hw") continue;
            params[pair.first] = pair.second;
        }
        o["options"] = params; spec = spec.left(bracket);
    }
    o["transport"] = spec; return ParseFromJson(o);
}
QString olcrtc::Validate() const {
    if (!QStringList{"jitsi", "telemost", "wbstream"}.contains(provider)) return QStringLiteral("olcRTC: неизвестный сервис");
    if (!QStringList{"datachannel", "vp8channel", "seichannel", "videochannel"}.contains(transport)) return QStringLiteral("olcRTC: неизвестный транспорт");
    if (provider == "telemost" && (transport == "datachannel" || transport == "seichannel")) return QStringLiteral("Телемост не поддерживает этот транспорт");
    if (provider == "wbstream" && transport == "datachannel" && token.isEmpty()) return QStringLiteral("WB Stream DataChannel требует токен с правами публикации");
    if (room.trimmed().isEmpty() || room.contains('\n') || room.contains('\r')) return QStringLiteral("Укажите комнату olcRTC");
    if (provider == "jitsi") { const QUrl u(room.startsWith("https://") ? room : "https://" + room); if (!u.isValid() || u.scheme() != "https" || u.host().isEmpty() || u.path().size() < 2 || !u.userInfo().isEmpty()) return QStringLiteral("Укажите ссылку Jitsi вида https://сервер/комната"); }
    if (!QRegularExpression("^[0-9a-fA-F]{64}$").match(key).hasMatch()) return QStringLiteral("Ключ olcRTC должен содержать 64 шестнадцатеричных символа");
    const QUrl resolver("tcp://" + dns); if (resolver.host().isEmpty() || resolver.port() < 1 || resolver.port() > 65535 || !resolver.userInfo().isEmpty() || !resolver.path().isEmpty()) return QStringLiteral("DNS: укажите адрес и порт");
    const QMap<QString, QPair<int,int>> limits{{"vp8-fps", {1,120}}, {"vp8-batch", {1,64}}, {"fps", {1,120}}, {"batch", {1,64}}, {"frag", {1,65535}}, {"ack-ms", {1,60000}}, {"video-w", {1,4096}}, {"video-h", {1,4096}}, {"video-fps", {1,120}}, {"video-qr-size", {0,65535}}, {"video-tile-module", {1,270}}, {"video-tile-rs", {0,200}}};
    for (auto it = options.begin(); it != options.end(); ++it) {
        if (it.key() == "video-codec") { if (!QStringList{"qrcode", "tile"}.contains(it.value().toString())) return QStringLiteral("Неверный видеокодек"); }
        else if (it.key() == "video-qr-recovery") { if (!QStringList{"low", "medium", "high", "highest"}.contains(it.value().toString())) return QStringLiteral("Неверное восстановление QR"); }
        else { bool ok; const int n = it.value().toVariant().toString().toInt(&ok); if (!ok || !limits.contains(it.key()) || n < limits[it.key()].first || n > limits[it.key()].second) return QStringLiteral("Неверный параметр olcRTC: ") + it.key(); }
    }
    if (options["video-codec"].toString() == "tile" && (options["video-w"].toVariant().toInt() != 1080 || options["video-h"].toVariant().toInt() != 1080)) return QStringLiteral("Для Tile нужны размеры 1080 × 1080");
    return {};
}
QJsonObject olcrtc::ExportToJson() { return {{"type","olcrtc"},{"name",name},{"provider",provider},{"transport",transport},{"room",room},{"key",key},{"token",token},{"channel",channel},{"dns",dns},{"options",options},{"device_id",deviceID}}; }
QJsonObject olcrtc::ExportIdentity() { auto o = ExportToJson(); o.remove("name"); o.remove("device_id"); return o; }
QString olcrtc::ExportToLink() {
    if (!Validate().isEmpty() || !token.isEmpty() || !channel.isEmpty()) return {}; // URI v1 has no account-token or channel field.
    QString spec = transport; QStringList params;
    for (auto it = options.begin(); it != options.end(); ++it) params << it.key() + "=" + QString::fromLatin1(QUrl::toPercentEncoding(it.value().toVariant().toString()));
    if (!params.isEmpty()) spec += "<" + params.join('&') + ">";
    return "olcrtc://" + provider + "?" + spec + "@" + room + "#" + key + (name.isEmpty() ? "" : "$" + QString::fromLatin1(QUrl::toPercentEncoding(name)));
}
QString olcrtc::Prepare() {
    if (const auto error = Validate(); !error.isEmpty()) return error;
    QString executable = "olcrtc";
#ifdef Q_OS_WIN
    executable += ".exe";
#endif
    extraCorePath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(executable);
    if (!socksPort) { const auto ports = MkManyPorts(1); if (ports.isEmpty()) return QStringLiteral("Не удалось открыть локальный порт olcRTC"); socksPort = ports.first(); }
    if (deviceID.isEmpty()) deviceID = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bridgeAuth = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto config = ExportToJson(); config["backend"] = "olcrtc"; config["peer"] = provider + ":" + room; config["socks"] = "127.0.0.1:" + QString::number(socksPort);
    config["socks_user"] = bridgeAuth; config["socks_pass"] = bridgeAuth;
    extraCoreConf = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact)); extraCoreArgs = "--qthrone-config %s"; noLogs = true;
    return {};
}
}
