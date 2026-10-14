#include "include/configs/sub/ProviderRouting.h"
#include "include/configs/common/utils.h"
#include "include/global/Configs.hpp"
#include "include/global/HTTPRequestHelper.hpp"
#include "include/global/Utils.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrlQuery>

namespace Subscription::ProviderRouting {
namespace {
const QRegularExpression
    link(QStringLiteral("^(happ|incy)?://routing/(add|onadd|off)(?:/([^?#]*))?(?:\\?data=([^#]*))?(?:#.*)?$"),
         QRegularExpression::CaseInsensitiveOption);
QMutex geoMutex;
QString digest(const QString &text) {
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QString unwrap(QString text) {
    text = text.trimmed();
    const QUrl url(text);
    if (url.scheme() == "http" || url.scheme() == "https") {
        const auto nested =
            QUrl::fromPercentEncoding(QUrlQuery(url).queryItemValue("url", QUrl::FullyEncoded).toUtf8());
        if (link.match(nested).hasMatch())
            return nested;
    }
    return text;
}
QJsonObject decode(const QString &text) {
    auto obj = QJsonDocument::fromJson(text.toUtf8()).object();
    if (!obj.isEmpty())
        return obj;
    for (auto option : {QByteArray::Base64Encoding, QByteArray::Base64UrlEncoding}) {
        obj = QJsonDocument::fromJson(
                  QByteArray::fromBase64(text.toUtf8(), option | QByteArray::AbortOnBase64DecodingErrors))
                  .object();
        if (!obj.isEmpty())
            return obj;
    }
    return {};
}
QString geoFile(const QString &url) { return Configs::GetBasePath() + "/provider-geodata/" + digest(url) + ".dat"; }
QString download(const QJsonObject &doc, const QString &kind, QString &path, bool force) {
    const auto address = GeoUrl(doc, kind);
    const QUrl url(address);
    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "https" && url.scheme() != "http"))
        return QObject::tr("Invalid Geo URL: %1").arg(address);
    path = geoFile(address);
    const QFileInfo cached(path);
    if (!force && cached.size() > 0 && cached.lastModified().secsTo(QDateTime::currentDateTime()) < 7 * 86400)
        return {};
    QDir().mkpath(cached.absolutePath());
    HttpGetOptions options;
    options.maxBytes = 64 * 1024 * 1024;
    options.directWhenDisconnected = true;
    options.transferTimeoutMs = 30000;
    const auto response = NetworkRequestHelper::HttpGet(address, options);
    if (!response.error.isEmpty())
        return QObject::tr("Could not download %1 from %2: %3").arg(kind, address, response.error);
    if (response.data.isEmpty())
        return QObject::tr("Empty Geo file: %1").arg(address);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(response.data) != response.data.size() || !file.commit())
        return QObject::tr("Could not save Geo file: %1").arg(path);
    return {};
}
} // namespace
bool IsLink(const QString &input) { return input.size() <= 2 * 1024 * 1024 && link.match(unwrap(input)).hasMatch(); }
bool Parse(const QString &input, Command &command, QString &error, const QString &provider) {
    if (input.size() > 2 * 1024 * 1024) {
        error = QObject::tr("Routing profile is too large.");
        return false;
    }
    const auto raw = unwrap(input);
    const auto match = link.match(raw);
    command = {};
    command.provider = match.captured(1).isEmpty()
                           ? provider
                           : (match.captured(1).compare("incy", Qt::CaseInsensitive) == 0 ? "Incy" : "Happ");
    if (raw.compare("off", Qt::CaseInsensitive) == 0 || match.captured(2).compare("off", Qt::CaseInsensitive) == 0) {
        command.off = true;
        return true;
    }
    const auto payload =
        match.hasMatch()
            ? QUrl::fromPercentEncoding((match.captured(3).isEmpty() ? match.captured(4) : match.captured(3)).toUtf8())
            : raw;
    command.document = decode(payload);
    if (command.document.isEmpty() ||
        (!command.document.contains("GlobalProxy") && !command.document.contains("DirectSites") &&
         !command.document.contains("Name"))) {
        error =
            QObject::tr("The %1 routing profile is incomplete or damaged. Copy the whole link.").arg(command.provider);
        return false;
    }
    for (const auto key : {"DirectSites", "DirectIp", "ProxySites", "ProxyIp", "BlockSites", "BlockIp"}) {
        const auto value = command.document.value(key);
        if (!value.isUndefined() && (!value.isArray() || value.toArray().size() > 100000)) {
            error = QObject::tr("Invalid routing list: %1").arg(key);
            return false;
        }
        for (const auto entry : value.toArray()) {
            if (!entry.isString()) {
                error = QObject::tr("Invalid routing entry: %1").arg(key);
                return false;
            }
            const auto text = entry.toString().trimmed();
            if (QString::fromLatin1(key).endsWith("Ip") && !text.isEmpty() && !text.startsWith("geoip:") &&
                !text.startsWith("geosite:")) {
                const auto network = QHostAddress::parseSubnet(text);
                const QHostAddress address(text);
                if (network.first.isNull() && address.isNull()) {
                    error = QObject::tr("Invalid IP network: %1").arg(text);
                    return false;
                }
            }
        }
    }
    for (const auto key : {"Geositeurl", "Geoipurl"}) {
        const auto address = command.document.value(key).toString().trimmed();
        if (address.isEmpty())
            continue;
        const QUrl url(address);
        if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "http" && url.scheme() != "https")) {
            error = QObject::tr("Invalid Geo URL: %1").arg(address);
            return false;
        }
    }
    command.activate = match.captured(2).compare("onadd", Qt::CaseInsensitive) == 0;
    return true;
}
Payload Extract(const QByteArray &body, const QString &header, bool ignore) {
    Payload result;
    QString text = QString::fromUtf8(body).trimmed();
    if (!text.contains("://") && !text.startsWith('{') && !text.startsWith('[')) {
        auto compact = text.toUtf8();
        compact.replace("\n", "");
        compact.replace("\r", "");
        compact.replace(" ", "");
        const auto decoded = QByteArray::fromBase64(compact, QByteArray::AbortOnBase64DecodingErrors);
        if (decoded.contains("://"))
            text = QString::fromUtf8(decoded);
    }
    const auto add = [&](const QString &value) {
        if (ignore)
            return;
        Command command;
        QString error;
        if (Parse(value, command, error))
            result.commands.append(command);
        else
            result.warnings.append(error);
    };
    if (!header.trimmed().isEmpty())
        add(header);
    QStringList lines;
    for (const auto &line : text.split('\n')) {
        if (IsLink(line))
            add(line);
        else
            lines.append(line);
    }
    result.body = lines.join('\n').toUtf8();
    return result;
}
void Apply(Configs::SubscriptionOptions &options, const QList<Command> &commands) {
    if (options.provider_ignore_import)
        return;
    auto profiles = QJsonDocument::fromJson(options.provider_routing_profiles.toUtf8()).array();
    for (const auto &command : commands) {
        options.routing_provider = command.provider;
        if (command.off) {
            options.provider_routing = "off";
            continue;
        }
        const auto doc = command.document;
        const auto name = doc.value("Name").toString();
        int index = -1;
        for (int i = 0; i < profiles.size(); ++i)
            if (profiles[i].toObject().value("Name").toString() == name) {
                index = i;
                break;
            }
        if (index >= 0) {
            const auto old = profiles[index].toObject().value("LastUpdated").toVariant().toLongLong();
            const auto stamp = doc.value("LastUpdated").toVariant().toLongLong();
            if (stamp > 0 && old > 0 && stamp < old)
                continue;
            profiles[index] = doc;
            if (decode(options.provider_routing).value("Name").toString() == name)
                options.provider_routing = QJsonDocument(doc).toJson(QJsonDocument::Compact);
        } else
            profiles.append(doc);
        if (options.provider_routing.isEmpty() || command.activate)
            options.provider_routing = QJsonDocument(doc).toJson(QJsonDocument::Compact);
    }
    options.provider_routing_profiles = QJsonDocument(profiles).toJson(QJsonDocument::Compact);
}
bool Truth(const QJsonObject &doc, const QString &key, bool fallback) {
    const auto value = doc.value(key);
    if (value.isBool())
        return value.toBool();
    if (value.isString())
        return value.toString().compare("true", Qt::CaseInsensitive) == 0 || value.toString() == "1";
    return fallback;
}
std::shared_ptr<Configs::RouteProfile> Profile(const QJsonObject &doc, const QString &provider) {
    auto profile = Configs::RouteProfile::GetDefaultChain();
    profile->name = doc.value("Name").toString(provider + " Default");
    profile->defaultOutboundID = Truth(doc, "GlobalProxy", true) ? Configs::proxyID : Configs::directID;
    auto order = doc.value("RouteOrder").toString().toLower().split('-');
    if (order.size() != 3 || QSet<QString>(order.begin(), order.end()) != QSet<QString>{"block", "proxy", "direct"})
        order = {"block", "proxy", "direct"};
    for (auto item : order) {
        const auto outbound = Configs::stringToOutboundID(item);
        item[0] = item[0].toUpper();
        for (const auto &suffix : {QStringLiteral("Sites"), QStringLiteral("Ip")}) {
            auto rule = std::make_shared<Configs::RouteRule>();
            rule->name = item + suffix;
            rule->outboundID = outbound;
            for (const auto &value : doc.value(rule->name).toArray()) {
                const auto text = value.toString().trimmed();
                if (text.isEmpty())
                    continue;
                if (text.startsWith("geosite:") || text.startsWith("geoip:"))
                    rule->rule_set << text;
                else if (suffix == "Ip") {
                    const QHostAddress address(text);
                    rule->ip_cidr << (text.contains('/')
                                          ? text
                                          : address.toString() +
                                                (address.protocol() == QAbstractSocket::IPv4Protocol ? "/32" : "/128"));
                } else if (text.startsWith("full:"))
                    rule->domain << text.mid(5);
                else if (text.startsWith("regexp:"))
                    rule->domain_regex << text.mid(7);
                else if (text.startsWith("keyword:"))
                    rule->domain_keyword << text.mid(8);
                else
                    rule->domain_suffix << (text.startsWith("domain:") ? text.mid(7) : text);
            }
            // RouteRule::isEmpty serializes the action; an empty block becomes a
            // catch-all reject and an empty route retains its outbound. Only
            // append provider rules that actually contain matching conditions.
            if (!rule->domain.isEmpty() || !rule->domain_suffix.isEmpty() || !rule->domain_keyword.isEmpty() ||
                !rule->domain_regex.isEmpty() || !rule->rule_set.isEmpty() || !rule->ip_cidr.isEmpty())
                profile->Rules.append(rule);
        }
    }
    if (doc.value("DomainStrategy").toString() == "IPIfNonMatch") {
        QList<std::shared_ptr<Configs::RouteRule>> domains, ips;
        for (const auto &rule : profile->Rules)
            (rule->name.endsWith("Ip") ? ips : domains).append(rule);
        if (!ips.isEmpty()) {
            auto resolve = std::make_shared<Configs::RouteRule>();
            resolve->name = "Resolve unmatched domains";
            resolve->action = "resolve";
            resolve->strategy = "prefer_ipv4";
            domains.append(resolve);
        }
        domains.append(ips);
        profile->Rules = domains;
    }
    return profile;
}
QString GeoUrl(const QJsonObject &doc, const QString &kind) {
    const auto url = doc.value(kind == "geoip" ? "Geoipurl" : "Geositeurl").toString().trimmed();
    return url.isEmpty() ? "https://github.com/Loyalsoldier/v2ray-rules-dat/releases/latest/download/" + kind + ".dat"
                         : url;
}
QString PrepareGeo(const QJsonObject &doc, const QString &kind, QString &path, bool force) {
    QMutexLocker lock(&geoMutex);
    return download(doc, kind, path, force);
}
QString ResolveGeo(Configs::RouteProfile &profile, const QJsonObject &doc) {
    QMutexLocker lock(&geoMutex);
    for (const auto &rule : profile.Rules)
        for (auto &selector : rule->rule_set) {
            const auto kind = selector.section(':', 0, 0);
            if (kind != "geosite" && kind != "geoip")
                continue;
            QString source;
            if (const auto error = download(doc, kind, source, false); !error.isEmpty())
                return error;
            const auto target = QFileInfo(source).absolutePath() + "/" +
                                digest(GeoUrl(doc, kind) + selector +
                                       QString::number(QFileInfo(source).lastModified().toMSecsSinceEpoch())) +
                                ".srs";
            if (!QFileInfo::exists(target)) {
                QProcess process;
                process.start(Configs::FindCoreRealPath(),
                              {"--convert-geo", source, target, kind, selector.section(':', 1)});
                if (!process.waitForStarted(10000) || !process.waitForFinished(60000)) {
                    process.kill();
                    process.waitForFinished();
                    return QObject::tr("Geo conversion timed out.");
                }
                if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
                    return QString::fromUtf8(process.readAllStandardError());
            }
            selector = QUrl::fromLocalFile(target).toString();
        }
    return {};
}
QString ResolveXrayGeo(const QJsonObject &document, QJsonObject &config) {
    auto doc = document;
    if (doc.isEmpty()) {
        doc["Geositeurl"] = Configs::dataManager->settingsRepo->xray_geosite_url;
        doc["Geoipurl"] = Configs::dataManager->settingsRepo->xray_geoip_url;
    }
    const auto text = QJsonDocument(config).toJson(QJsonDocument::Compact);
    QMap<QString, QString> files;
    for (const auto &kind : {QStringLiteral("geosite"), QStringLiteral("geoip")}) {
        if (!text.contains((kind + ':').toUtf8()))
            continue;
        QString source;
        if (const auto error = PrepareGeo(doc, kind, source); !error.isEmpty())
            return error;
        files[kind] = QDir(Configs::GetBasePath()).relativeFilePath(source);
    }
    std::function<QJsonValue(const QJsonValue &)> replace = [&](const QJsonValue &value) -> QJsonValue {
        if (value.isString()) {
            auto rule = value.toString();
            const bool inverse = rule.startsWith('!');
            if (inverse)
                rule.remove(0, 1);
            for (auto it = files.begin(); it != files.end(); ++it) {
                if (rule.startsWith(it.key() + ':'))
                    return (inverse ? "!" : "") + QString(it.key() == "geoip" ? "ext-ip:" : "ext:") + it.value() + ':' +
                           rule.mid(it.key().size() + 1);
            }
        } else if (value.isArray()) {
            QJsonArray result;
            for (const auto &entry : value.toArray())
                result.append(replace(entry));
            return result;
        } else if (value.isObject()) {
            QJsonObject result;
            const auto object = value.toObject();
            for (auto it = object.begin(); it != object.end(); ++it)
                result[replace(it.key()).toString()] = replace(it.value());
            return result;
        }
        return value;
    };
    config = replace(config).toObject();
    return {};
}
QString Dns(const QJsonObject &doc, const QString &prefix, const QString &fallback) {
    const auto domain = doc.value(prefix + "DNSDomain").toString();
    const auto type = doc.value(prefix + "DNSType").toString();
    if (type.compare("DoH", Qt::CaseInsensitive) == 0 && !domain.isEmpty())
        return domain;
    const auto ip = doc.value(prefix + "DNSIP").toString(doc.value(prefix + "Dns").toString());
    if (type.compare("TCP", Qt::CaseInsensitive) == 0 && !ip.isEmpty())
        return ip.contains("://") ? ip : "tcp://" + ip;
    if (type.compare("DoT", Qt::CaseInsensitive) == 0) {
        const auto host = domain.isEmpty() ? ip : domain;
        if (!host.isEmpty())
            return host.contains("://") ? host : "tls://" + host;
    }
    return ip.isEmpty() ? fallback : ip;
}
QStringList Hosts(const QJsonObject &doc) {
    QStringList hosts;
    const auto entries = doc.value("DnsHosts").toObject();
    for (auto it = entries.begin(); it != entries.end(); ++it)
        hosts << it.value().toString() + " " + it.key();
    for (const auto &prefix : {QStringLiteral("Remote"), QStringLiteral("Domestic")}) {
        const auto host = QUrl(doc.value(prefix + "DNSDomain").toString()).host();
        const auto ip = doc.value(prefix + "DNSIP").toString();
        if (!host.isEmpty() && !ip.isEmpty() && !entries.contains(host))
            hosts << ip + " " + host;
    }
    return hosts;
}
} // namespace Subscription::ProviderRouting
