#include "include/configs/outbounds/openfluxkey.h"
#include "include/global/Utils.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

namespace Configs {
namespace {
const QString prefix = QStringLiteral("openflux://import");
// openflux-app turns end-to-end encryption off for these: their carriers cannot hold it.
const QStringList noE2E{"mailru", "boards", "telemost", "cupsonline", "avito"};
const QStringList keys{"transport", "doc_url", "doc_urls", "key_token", "control_url", "e2e_encryption", "max_token", "max_uid", "mtu"};
}

// The bridge's transports (bridge/mobile.go buildTransport), "script" aside: it needs the fork's script packages.
const QStringList &openfluxkey::Transports() {
    static const QStringList list{"yandex", "volga", "max", "boards", "mailru", "mts", "telemost", "cupsonline", "avito", "yandex_multistream"};
    return list;
}

QStringList openfluxkey::documents() const {
    QStringList list{data["doc_url"].toString()};
    for (const auto &value : data["doc_urls"].toArray()) list << value.toString();
    list.removeAll(QString());
    return list;
}

bool openfluxkey::ParseFromJson(const QJsonObject &o) {
    if (o["type"].toString() != "openfluxkey") return false;
    data = {};
    for (const auto &key : keys)
        if (o.contains(key)) data[key] = o[key];
    name = o["name"].toString(o["tag"].toString());
    data["transport"] = data["transport"].toString().toLower().isEmpty() ? QStringLiteral("yandex") : data["transport"].toString().toLower();
    if (noE2E.contains(data["transport"].toString())) data.remove("e2e_encryption");
    server = DisplayAddress();
    server_port = 443;
    return Validate().isEmpty();
}

QString openfluxkey::Validate() const {
    const auto transport = data["transport"].toString();
    if (!Transports().contains(transport)) return QStringLiteral("OpenFlux Key: неизвестный транспорт");
    if (transport == "max") {
        if (data["max_token"].toString().trimmed().isEmpty() || data["max_uid"].toVariant().toLongLong() == 0)
            return QStringLiteral("OpenFlux Key: для MAX нужны токен и ID");
        return {};
    }
    const auto docs = documents();
    if (docs.isEmpty()) return QStringLiteral("OpenFlux Key: укажите ссылку на документ или доску");
    for (const auto &address : docs) {
        const QUrl url(address);
        if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty()) return QStringLiteral("OpenFlux Key: ссылка на документ вида https://…");
    }
    return {};
}

QString openfluxkey::DisplayAddress() {
    const auto docs = documents();
    return docs.isEmpty() ? QString() : QUrl(docs.first()).host();
}

bool openfluxkey::ParseFromLink(const QString &link) {
    const auto text = link.trimmed();
    if (!text.startsWith(prefix, Qt::CaseInsensitive) || text.size() > 65536) return false;
    const QUrlQuery query(QUrl(text).query());
    auto body = query.queryItemValue("data", QUrl::FullyDecoded);
    // Chats change the alphabet, wrap lines or drop the padding.
    body.remove(QRegularExpression("[\\s\\x{200b}=]"));
    body.replace('+', '-').replace('/', '_');
    const auto json = QByteArray::fromBase64Encoding(body.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals | QByteArray::AbortOnBase64DecodingErrors);
    if (!json) return false;
    QJsonParseError error{};
    auto object = QJsonDocument::fromJson(*json, &error).object();
    if (error.error != QJsonParseError::NoError) return false;
    object["type"] = "openfluxkey";
    return ParseFromJson(object);
}

QJsonObject openfluxkey::ExportToJson() {
    auto o = data;
    o["type"] = "openfluxkey";
    if (!name.isEmpty()) o["name"] = name;
    return o;
}

QJsonObject openfluxkey::ExportIdentity() { auto o = ExportToJson(); o.remove("name"); return o; }

// The link the fork's control plane and app write: the same fields, base64url without padding.
QString openfluxkey::ExportToLink() {
    if (!Validate().isEmpty()) return {};
    QJsonObject payload{{"name", name}, {"mode", "key"}};
    for (const auto *key : {"control_url", "key_token", "doc_url", "doc_urls", "transport", "e2e_encryption", "max_token", "max_uid"})
        if (data.contains(QString::fromLatin1(key))) payload[QString::fromLatin1(key)] = data[QString::fromLatin1(key)];
    return prefix + "?data=" + QString::fromLatin1(QJsonDocument(payload).toJson(QJsonDocument::Compact)
                                                     .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString openfluxkey::Prepare() {
    if (const auto error = Validate(); !error.isEmpty()) return error;
    QString executable = "openfluxkey";
#ifdef Q_OS_WIN
    executable += ".exe";
#endif
    extraCorePath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(executable);
    if (!QFileInfo(extraCorePath).isExecutable()) return QStringLiteral("OpenFlux Key: клиент не входит в эту сборку");
    if (socksPort == 0) { const auto ports = MkManyPorts(1); if (ports.isEmpty()) return QStringLiteral("OpenFlux Key: не удалось открыть локальный порт"); socksPort = ports.first(); }
    bridgeAuth = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto config = data;
    config["backend"] = "openfluxkey";
    config["socks"] = "127.0.0.1:" + QString::number(socksPort);
    config["socks_user"] = bridgeAuth;
    config["socks_pass"] = bridgeAuth;
    // One board or document carries one client of the fork's exit: profiles on the same one share its session.
    auto docs = documents();
    docs.sort();
    config["peer"] = "openfluxkey:" + docs.join(',');
    config["device_id"] = QString::fromLatin1(QCryptographicHash::hash(data["key_token"].toString().toUtf8(), QCryptographicHash::Sha256).toHex());
    extraCoreConf = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
    extraCoreArgs = "--qthrone-config %s";
    noLogs = true;
    return {};
}

BuildResult openfluxkey::Build() {
    if (socksPort == 0 || bridgeAuth.isEmpty()) return {{}, "OpenFlux Key bridge has not been prepared"};
    return {QJsonObject{{"type", "socks"}, {"server", socksAddress}, {"server_port", socksPort},
                        {"version", "5"}, {"username", bridgeAuth}, {"password", bridgeAuth}}, {}};
}
}
