#include "include/configs/outbounds/wdttplus.h"
#include "include/configs/outbounds/qwdtt.h"
#include "include/global/Configs.hpp"
#include "include/ui/setting/dialog_vk_relay.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>
#include <QTcpServer>
#include <QTimeZone>
#include <QUdpSocket>
#include <QUrl>
#include <QUuid>

namespace {
bool wdttValidPort(int p) { return p >= 1 && p <= 65535; }
QString wdttEndpoint(const QString &host, int port) { return (host.contains(':') ? "[" + host + "]" : host) + ":" + QString::number(port); }
QString wdttEncode(const QString &value) { return QString::fromLatin1(QUrl::toPercentEncoding(value)); }
QString wdttDecode(const QString &value) { return QUrl::fromPercentEncoding(value.toUtf8()); }
}

namespace Configs {
wdttplus::wdttplus() {
    server_port = 56000; socksPort = 0; deviceID = RelayDeviceID();
    // New profiles follow the default hash source of the VK binding, as qWDTT and CSQTT do.
    if (dataManager) { const auto *settings = dataManager->settingsRepo.get(); hashSource = settings->vk_relay_linked ? settings->qwdtt_hash_source : "manual"; autoHashCount = qBound(1, settings->qwdtt_auto_hash_count, 4); }
}

int wdttplus::WorkerLimit(int profileMaxWorkers) {
    const int configured = profileMaxWorkers / 9 * 9;
    return configured >= 9 ? qMin(configured, 108) : 108;
}

int wdttplus::NormalizeWorkers(int requested, int profileMaxWorkers) {
    const int rounded = (qMax(requested, 9) + 4) / 9 * 9;
    return qBound(9, rounded, WorkerLimit(profileMaxWorkers));
}

bool wdttplus::ValidTurnSni(const QString &value) {
    if (value.isEmpty()) return true;
    if (value.size() > 253 || !value.contains('.') || !QHostAddress(value).isNull()) return false;
    const QRegularExpression label("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$");
    for (const auto &part : value.split('.')) if (!label.match(part).hasMatch()) return false;
    return true;
}

QString wdttplus::DeviceInfo() {
    const QLocale locale;
    const QJsonObject info{{"name", QSysInfo::prettyProductName()}, {"manufacturer", ""}, {"brand", ""},
        {"model", QSysInfo::prettyProductName()}, {"os", QSysInfo::productType()}, {"os_version", QSysInfo::productVersion()},
        {"abi", QSysInfo::currentCpuArchitecture()}, {"app_version", "qThrone"}, {"locale", locale.bcp47Name()},
        {"country", QLocale::territoryToCode(locale.territory())}, {"time_zone", QString::fromUtf8(QTimeZone::systemTimeZoneId())}};
    return QString::fromUtf8(QJsonDocument(info).toJson(QJsonDocument::Compact));
}

QJsonObject wdttplus::LegacyToQwdtt(const QJsonObject &object) {
    if (object["type"].toString() != "wdttplus" || !object["legacy"].toBool(false)) return {};
    auto copy = object;
    copy["type"] = "qwdtt";
    for (const auto *key : {"legacy", "wg_port", "local_port", "max_workers", "workers"}) copy.remove(key);
    qwdtt profile;
    return profile.ParseFromJson(copy) ? profile.ExportToJson() : QJsonObject{};
}

bool wdttplus::ParseFromJson(const QJsonObject &object) {
    if (object["type"].toString() != "wdttplus") return false;
    outbound::ParseFromJson(object);
    password = object["password"].toString();
    hashSource = object["hash_source"].toString(hashSource);
    if (hashSource != "auto" && hashSource != "manual") return false;
    autoHashCount = qBound(1, object["auto_hash_count"].toInt(autoHashCount), 4);
    QStringList values;
    const auto h = object["hashes"];
    if (h.isArray()) { for (const auto &v : h.toArray()) values.append(v.toString()); }
    else values.append(h.toString());
    hashes = qwdtt::NormalizeHashes(values).mid(0, 4);
    wgPort = object["wg_port"].toInt(56001);
    localPort = object["local_port"].toInt(9000);
    maxWorkers = object["max_workers"].toInt(0);
    legacy = object["legacy"].toBool(false);
    workers = NormalizeWorkers(object["workers"].toInt(18), maxWorkers);
    captchaMode = object["captcha_mode"].toString("auto");
    tcpOnly = object["tcp_only"].toBool(); rtMasque = object["rt_masque"].toBool();
    rtMasqueServerBootstrap = object["rt_masque_server_bootstrap"].toBool();
    vkCallsPreflight = object["vkcalls_preflight"].toBool(true);
    turnSni = object["turn_sni"].toString().trimmed().toLower();
    fingerprint = object["fingerprint"].toString("firefox");
    clientIds = object["client_ids"].toString("6287487,8202606");
    if (!object["device_id"].toString().isEmpty()) deviceID = object["device_id"].toString();
    return !server.isEmpty() && wdttValidPort(server_port) && wdttValidPort(wgPort) && wdttValidPort(localPort)
        && (captchaMode == "auto" || captchaMode == "wv" || captchaMode == "rjs") && ValidTurnSni(turnSni)
        && QStringList{"firefox","chrome","safari","ios","android","random"}.contains(fingerprint)
        && QRegularExpression("^[0-9]+(?:,[0-9]+){0,15}$").match(clientIds).hasMatch();
}

bool wdttplus::ParseFromLink(const QString &link) {
    auto value = link.trimmed();
    if (!value.startsWith("wdtt://", Qt::CaseInsensitive)) return false;
    value.remove(0, 7);
    if (!value.startsWith("connect?", Qt::CaseInsensitive)) {
        // wdtt://<server>:<dtls>:<wg>:<local>:<password>[:<hashes>][#name], the original WDTT form.
        QString label;
        if (const int fragment = value.indexOf('#'); fragment >= 0) { label = wdttDecode(value.mid(fragment + 1)); value.truncate(fragment); }
        const auto parts = value.split(':');
        if (parts.size() < 5 || parts.size() > 6) return false;
        bool dtlsOK, wgOK, localOK;
        const int dtls = parts[1].toInt(&dtlsOK), wg = parts[2].toInt(&wgOK), local = parts[3].toInt(&localOK);
        if (!dtlsOK || !wgOK || !localOK || parts[0].isEmpty() || parts[4].isEmpty()) return false;
        return ParseFromJson({{"type", "wdttplus"}, {"legacy", true}, {"server", parts[0]}, {"server_port", dtls}, {"wg_port", wg},
            {"local_port", local}, {"password", wdttDecode(parts[4])}, {"hashes", parts.size() == 6 ? wdttDecode(parts[5]) : QString()}, {"tag", label}});
    }
    // wdtt://connect?v=1&host&dtls&wg&local&password&hashes[&name][&max_workers], as its TransferCodec builds it.
    QString label;
    auto query = value.mid(8);
    if (const int fragment = query.indexOf('#'); fragment >= 0) { label = wdttDecode(query.mid(fragment + 1)); query.truncate(fragment); }
    QMap<QString, QString> q;
    for (const auto &part : query.split('&', Qt::SkipEmptyParts)) {
        const int at = part.indexOf('=');
        // Form encoding: '+' is a space unless escaped as %2B.
        if (at > 0) q[part.left(at).toLower()] = wdttDecode(QString(part.mid(at + 1)).replace('+', ' '));
    }
    if (q.value("v") != "1") return false;
    bool dtlsOK, wgOK, localOK;
    const int dtls = q.value("dtls").toInt(&dtlsOK), wg = q.value("wg").toInt(&wgOK), local = q.value("local").toInt(&localOK);
    if (q.value("host").trimmed().isEmpty() || !dtlsOK || !wdttValidPort(dtls) || !wgOK || !localOK) return false;
    QJsonObject object{{"type", "wdttplus"}, {"server", q.value("host").trimmed()}, {"server_port", dtls}, {"wg_port", wg},
        {"local_port", local}, {"password", q.value("password")}, {"hashes", q.value("hashes")},
        {"tag", q.contains("name") ? q.value("name") : label}};
    if (q.contains("max_workers")) {
        bool ok;
        const int limit = q.value("max_workers").toInt(&ok);
        if (!ok || limit < 9 || limit > 108 || limit % 9 != 0) return false;
        object["max_workers"] = limit;
        object["workers"] = limit;
    }
    return ParseFromJson(object);
}

QJsonObject wdttplus::ExportToJson() {
    QJsonObject object{{"type", "wdttplus"}, {"tag", name}, {"server", server}, {"server_port", server_port},
        {"wg_port", wgPort}, {"local_port", localPort}, {"password", password}, {"hashes", QJsonArray::fromStringList(hashes)},
        {"workers", workers}, {"captcha_mode", captchaMode}, {"device_id", deviceID},
        {"manual_hashes_override", manualRelayHashes}, {"hash_source", hashSource}, {"auto_hash_count", autoHashCount}};
    if (maxWorkers > 0) object["max_workers"] = maxWorkers;
    if (legacy) object["legacy"] = true;
    object["tcp_only"] = tcpOnly; object["turn_sni"] = turnSni;
    object["rt_masque"] = rtMasque; object["rt_masque_server_bootstrap"] = rtMasqueServerBootstrap;
    object["fingerprint"] = fingerprint; object["client_ids"] = clientIds; object["vkcalls_preflight"] = vkCallsPreflight;
    return object;
}

QJsonObject wdttplus::ExportIdentity() { auto object = ExportToJson(); object.remove("tag"); object.remove("device_id"); object.remove("hashes"); object.remove("workers"); object.remove("manual_hashes_override"); return object; }

QString wdttplus::ExportToLink() {
    // The device id is per installation and never travels in a link.
    if (legacy) return "wdtt://" + server + ":" + QString::number(server_port) + ":" + QString::number(wgPort) + ":" + QString::number(localPort)
        + ":" + password + ":" + hashes.join(',') + (name.isEmpty() ? QString() : "#" + wdttEncode(name));
    QString result = "wdtt://connect?v=1&host=" + wdttEncode(server) + "&dtls=" + QString::number(server_port) + "&wg=" + QString::number(wgPort)
        + "&local=" + QString::number(localPort) + "&password=" + wdttEncode(password) + "&hashes=" + wdttEncode(hashes.join(','));
    if (!name.isEmpty()) result += "&name=" + wdttEncode(name);
    if (maxWorkers >= 9 && maxWorkers <= 108) result += "&max_workers=" + QString::number(maxWorkers);
    return result;
}

QString wdttplus::Prepare() {
    hashes = qwdtt::NormalizeHashes(hashes);
    QStringList effectiveHashes = hashes;
    if (hashSource == "auto") {
        // The calls come from the qWDTT binding; WDTT Plus joins them without a VK login.
        QString error; effectiveHashes = qwdtt::NormalizeHashes(RelayAccountHashes(autoHashCount, &error)).mid(0, 4);
        if (!error.isEmpty()) return error;
    }
    workers = NormalizeWorkers(workers, maxWorkers);
    if (server.isEmpty() || !wdttValidPort(server_port)) return QStringLiteral("WDTT Plus: неверный адрес или порт сервера");
    if (password.isEmpty() || (password + deviceID).contains(QRegularExpression("[\\r\\n]"))) return QStringLiteral("WDTT Plus: неверный пароль подключения");
    if (effectiveHashes.isEmpty() || effectiveHashes.size() > 4) return QStringLiteral("WDTT Plus: укажите от одного до четырёх хешей звонков VK");
    const QRegularExpression valid("^[A-Za-z0-9_-]{16,512}$");
    for (const auto &hash : effectiveHashes) if (!valid.match(hash).hasMatch()) return QStringLiteral("WDTT Plus: некорректный хеш VK");
    if (deviceID.isEmpty()) deviceID = RelayDeviceID();
    QString helper = "qwdtt", client = "wdttplus-client";
#ifdef Q_OS_WIN
    helper += ".exe"; client += ".exe";
#endif
    const QDir directory(QCoreApplication::applicationDirPath());
    extraCorePath = directory.absoluteFilePath(helper);
    if (!QFileInfo(extraCorePath).isExecutable() || !QFileInfo(directory.absoluteFilePath(client)).isExecutable()) return QStringLiteral("WDTT Plus: клиент не найден рядом с программой");
    QTcpServer tcp; QUdpSocket udp;
    if (!tcp.listen(QHostAddress::LocalHost, 0) || !udp.bind(QHostAddress::LocalHost, 0)) return QStringLiteral("WDTT Plus: не удалось выделить локальные порты");
    // A new login and password for every launch; the client takes them on stdin.
    socksAddress = "127.0.0.1"; socksPort = tcp.serverPort();
    bridgeUser = QUuid::createUuid().toString(QUuid::Id128); bridgePass = QUuid::createUuid().toString(QUuid::Id128);
    const QJsonObject config{{"backend", "wdttplus"}, {"peer", wdttEndpoint(server, server_port)}, {"password", password},
        {"hashes", QJsonArray::fromStringList(effectiveHashes)}, {"workers", workers}, {"captcha_mode", captchaMode},
        {"device_id", deviceID}, {"device_info", DeviceInfo()}, {"listen", wdttEndpoint("127.0.0.1", udp.localPort())},
        {"socks", wdttEndpoint(socksAddress, socksPort)}, {"socks_user", bridgeUser}, {"socks_pass", bridgePass},
        {"tcp_only",tcpOnly},{"turn_sni",turnSni},{"rt_masque",rtMasque},{"rt_masque_server_bootstrap",rtMasqueServerBootstrap},
        {"fingerprint",fingerprint},{"client_ids",clientIds},{"vkcalls_preflight",vkCallsPreflight}};
    extraCoreConf = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact)); extraCoreArgs = "-wdttplus-config %s";
    return {};
}

BuildResult wdttplus::Build() {
    if (socksPort == 0 || bridgeUser.isEmpty()) return {{}, "WDTT Plus client has not been prepared"};
    return {QJsonObject{{"type", "socks"}, {"server", socksAddress}, {"server_port", socksPort}, {"version", "5"}, {"username", bridgeUser}, {"password", bridgePass}}, {}};
}
}
