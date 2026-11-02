#include "include/configs/outbounds/shadowsocksr.h"

#include <QUrlQuery>
#include "include/global/Utils.hpp"

namespace Configs {
    namespace {
        QString encode(const QString &value) {
            return QString::fromLatin1(value.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
        }

        bool decode(const QString &value, QString &result) {
            auto decoded = QByteArray::fromBase64Encoding(value.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded) decoded = QByteArray::fromBase64Encoding(value.toLatin1(), QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded) return false;
            result = QString::fromUtf8(decoded.decoded);
            return true;
        }

        QString alias(const QJsonObject &object, const QStringList &names, const QString &fallback = {}) {
            for (const auto &key : names) if (object.contains(key)) return object[key].toString();
            return fallback;
        }
    }

    bool shadowsocksr::ParseFromLink(const QString &link) {
        if (!link.startsWith("ssr://", Qt::CaseInsensitive)) return false;
        QString decoded;
        if (!decode(link.mid(6), decoded)) return false;
        const auto split = decoded.indexOf("/?");
        QStringList parts = (split < 0 ? decoded : decoded.left(split)).split(':');
        if (parts.size() < 6) return false;
        const auto encodedPassword = parts.takeLast();
        obfs = parts.takeLast().toLower();
        method = parts.takeLast().toLower();
        protocol = parts.takeLast().toLower();
        bool portOk;
        server_port = parts.takeLast().toInt(&portOk);
        server = parts.join(':');
        if (server.startsWith('[') && server.endsWith(']')) server = server.mid(1, server.size() - 2);
        if (!portOk || !decode(encodedPassword, password)) return false;
        const QUrlQuery query(split < 0 ? QString() : decoded.mid(split + 2));
        if (query.hasQueryItem("remarks") && !decode(query.queryItemValue("remarks", QUrl::FullyDecoded), name)) return false;
        if (query.hasQueryItem("protoparam") && !decode(query.queryItemValue("protoparam", QUrl::FullyDecoded), protocol_param)) return false;
        if (query.hasQueryItem("obfsparam") && !decode(query.queryItemValue("obfsparam", QUrl::FullyDecoded), obfs_param)) return false;
        return !server.isEmpty() && server_port > 0 && server_port <= 65535;
    }

    bool shadowsocksr::ParseFromJson(const QJsonObject &object) {
        const auto type = object["type"].toString();
        if (!type.isEmpty() && type != "ssr" && type != "shadowsocksr") return false;
        outbound::ParseFromJson(object);
        if (name.isEmpty()) name = alias(object, {"name", "remarks"});
        if (!object.contains("server_port")) server_port = object["port"].toVariant().toInt();
        method = alias(object, {"method", "cipher"}, method).trimmed().toLower();
        password = object["password"].toString();
        protocol = alias(object, {"protocol"}, protocol).trimmed().toLower();
        obfs = alias(object, {"obfs"}, obfs).trimmed().toLower();
        protocol_param = alias(object, {"protocol_param", "protocolparam", "protocol-param"});
        obfs_param = alias(object, {"obfs_param", "obfsparam", "obfs-param"});
        return !server.isEmpty();
    }

    bool shadowsocksr::ParseFromClash(const clash::Proxies &object) {
        if (object.type != "ssr") return false;
        outbound::ParseFromClash(object);
        method = QString::fromStdString(object.cipher).trimmed().toLower();
        password = QString::fromStdString(object.password);
        protocol = object.protocol.empty() ? "origin" : QString::fromStdString(object.protocol).trimmed().toLower();
        obfs = object.obfs.empty() ? "plain" : QString::fromStdString(object.obfs).trimmed().toLower();
        protocol_param = QString::fromStdString(object.protocol_param);
        obfs_param = QString::fromStdString(object.obfs_param);
        return true;
    }

    QString shadowsocksr::ExportToLink() {
        const auto endpoint = server + ':' + QString::number(server_port) + ':' + protocol + ':' + method + ':' + obfs + ':' + encode(password);
        QUrlQuery query;
        if (!name.isEmpty()) query.addQueryItem("remarks", encode(name));
        if (!protocol_param.isEmpty()) query.addQueryItem("protoparam", encode(protocol_param));
        if (!obfs_param.isEmpty()) query.addQueryItem("obfsparam", encode(obfs_param));
        return "ssr://" + encode(endpoint + "/?" + query.toString(QUrl::FullyEncoded));
    }

    QJsonObject shadowsocksr::ExportToJson() {
        auto object = outbound::ExportToJson();
        object["type"] = "shadowsocksr";
        object["method"] = method;
        object["password"] = password;
        object["protocol"] = protocol;
        object["obfs"] = obfs;
        if (!protocol_param.isEmpty()) object["protocol_param"] = protocol_param;
        if (!obfs_param.isEmpty()) object["obfs_param"] = obfs_param;
        return object;
    }

    BuildResult shadowsocksr::Build() {
        const auto base = outbound::Build();
        if (!base.error.isEmpty()) return base;
        auto object = ExportToJson();
        object.remove("tag");
        for (auto it = base.object.begin(); it != base.object.end(); ++it) object[it.key()] = it.value();
        return {object, {}};
    }

    void shadowsocksr::Validate(Validation::Issues &issues) {
        outbound::Validate(issues);
        using Validation::Severity;
        const auto check = [&](const QString &field, const QString &value, const QStringList &allowed) {
            if (auto error = Validation::OneOf(value, allowed, Validation::Empty::Required); !error.isEmpty())
                issues.append({Severity::Error, field, error, value});
        };
        check("method", method, shadowsocksrMethods);
        check("protocol", protocol, shadowsocksrProtocols);
        check("obfs", obfs, shadowsocksrObfs);
        if (method != "none" && password.isEmpty()) issues.append({Severity::Error, "password", Validation::Required(password)});
    }

    SecurityInfo shadowsocksr::GetSecurity() {
        SecurityInfo info;
        info.label = method == "none" ? QObject::tr("Raw") : QObject::tr("Weak Cipher");
        info.level = method == "none" ? SecurityLevel::None : SecurityLevel::Weak;
        return info;
    }
}
