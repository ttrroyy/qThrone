#include "include/configs/outbounds/s3bypass.h"
#include "include/global/Utils.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QUuid>

namespace Configs {
const QStringList &s3bypass::StorageKeys() {
    static const QStringList keys{"endpoint", "region", "bucket", "prefix", "sessionsDir", "accessKey", "secretKey"};
    return keys;
}

const QStringList &s3bypass::TuningKeys() {
    static const QStringList keys{"pollIntervalMs", "writeIntervalMs", "idleTimeoutSec", "maxFileSizeBytes"};
    return keys;
}

bool s3bypass::IsXrayConfig(const QJsonObject &config) {
    for (const auto &outbound : config["outbounds"].toArray())
        if (outbound.toObject()["protocol"].toString() == "fedarisha") return true;
    return false;
}

bool s3bypass::ParseFromXrayConfig(const QJsonObject &config) {
    for (const auto &item : config["outbounds"].toArray()) {
        const auto outbound = item.toObject();
        if (outbound["protocol"].toString() != "fedarisha") continue;
        const auto settings = outbound["settings"].toObject();
        auto label = config["remarks"].toString();
        if (label.isEmpty() && outbound["tag"].toString() != "proxy") label = outbound["tag"].toString();
        return ParseFromJson(QJsonObject{{"type", "s3bypass"}, {"name", label},
                                         {"storage", settings["storage"].toObject()}, {"tuning", settings["tuning"].toObject()}});
    }
    return false;
}

bool s3bypass::ParseFromJson(const QJsonObject &o) {
    if (o["type"].toString() != "s3bypass") return false;
    name = o["name"].toString(o["tag"].toString());
    storage = o["storage"].toObject();
    tuning = o["tuning"].toObject();
    server = QUrl(storage["endpoint"].toString()).host();
    server_port = 443;
    return Validate().isEmpty();
}

QString s3bypass::Validate() const {
    if (storage["type"].toString("s3") != "s3") return QStringLiteral("S3 Bypass: поддерживается только хранилище S3");
    for (const auto *key : {"bucket", "accessKey", "secretKey"})
        if (storage[QString::fromLatin1(key)].toString().trimmed().isEmpty()) return QStringLiteral("S3 Bypass: укажите корзину и ключи доступа");
    const QUrl endpoint(storage["endpoint"].toString());
    if (!endpoint.isValid() || (endpoint.scheme() != "http" && endpoint.scheme() != "https") || endpoint.host().isEmpty())
        return QStringLiteral("S3 Bypass: укажите адрес хранилища вида https://…");
    for (auto it = tuning.begin(); it != tuning.end(); ++it)
        if (!it.value().isDouble()) return QStringLiteral("S3 Bypass: параметры опроса и записи — числа");
    return {};
}

QString s3bypass::DisplayAddress() {
    const auto &st = storage;
    QStringList parts;
    if (const auto host = QUrl(st["endpoint"].toString()).host(); !host.isEmpty()) parts << host;
    if (const auto bucket = st["bucket"].toString(); !bucket.isEmpty()) parts << bucket;
    return parts.join('/');
}

QJsonObject s3bypass::ExportToJson() {
    QJsonObject o{{"type", "s3bypass"}, {"storage", storage}};
    if (!tuning.isEmpty()) o["tuning"] = tuning;
    if (!name.isEmpty()) o["name"] = name;
    return o;
}

QJsonObject s3bypass::ExportIdentity() { auto o = ExportToJson(); o.remove("name"); return o; }

QString s3bypass::Prepare() {
    const auto &st = storage;
    if (const auto error = Validate(); !error.isEmpty()) return error;
    QString executable = "s3bypass";
#ifdef Q_OS_WIN
    executable += ".exe";
#endif
    extraCorePath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(executable);
    if (!QFileInfo(extraCorePath).isExecutable()) return QStringLiteral("S3 Bypass: клиент не входит в эту сборку");
    if (socksPort == 0) { const auto ports = MkManyPorts(1); if (ports.isEmpty()) return QStringLiteral("S3 Bypass: не удалось открыть локальный порт"); socksPort = ports.first(); }
    bridgeAuth = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto config = ExportToJson();
    config.remove("name");
    config.remove("type");
    config["backend"] = "s3bypass";
    config["socks"] = "127.0.0.1:" + QString::number(socksPort);
    config["socks_user"] = bridgeAuth;
    config["socks_pass"] = bridgeAuth;
    // One session per bucket and prefix: the server reads its clients' objects there.
    config["peer"] = st["endpoint"].toString() + "/" + st["bucket"].toString() + "/" + st["prefix"].toString();
    config["device_id"] = QString::fromLatin1(QCryptographicHash::hash(st["accessKey"].toString().toUtf8(), QCryptographicHash::Sha256).toHex());
    extraCoreConf = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
    extraCoreArgs = "--qthrone-config %s";
    noLogs = true;
    return {};
}

BuildResult s3bypass::Build() {
    if (socksPort == 0 || bridgeAuth.isEmpty()) return {{}, "S3 Bypass bridge has not been prepared"};
    return {QJsonObject{{"type", "socks"}, {"server", socksAddress}, {"server_port", socksPort}, {"version", "5"},
                        {"username", bridgeAuth}, {"password", bridgeAuth}}, {}};
}
}
