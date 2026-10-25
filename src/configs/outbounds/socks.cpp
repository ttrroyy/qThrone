#include "include/configs/outbounds/socks.h"

#include <QUrlQuery>
#include <include/global/Utils.hpp>

#include "include/configs/common/utils.h"

namespace Configs {
    bool socks::ParseFromLink(const QString& link)
    {
        auto url = QUrl(link);
        if (!url.isValid()) return false;
        auto query = QUrlQuery(url.query());

        outbound::ParseFromLink(link);
        if (query.hasQueryItem("version"))
        {
            version = query.queryItemValue("version").toInt();
        } else
        {
            if (url.scheme() == "socks4") version = 4;
            if (url.scheme() == "socks5") version = 5;
        }
        
        if (!url.password().isEmpty() || !url.userName().isEmpty()) {
            username = url.userName();
            password = url.password();
            
            // v2rayN base64-encodes "user:pass" into the username.
            if (password.isEmpty() && !username.isEmpty()) {
                QString decoded = DecodeB64IfValid(username);
                if (!decoded.isEmpty()) {
                    username = SubStrBefore(decoded, ":");
                    password = SubStrAfter(decoded, ":");
                }
            }
        }

        if (query.hasQueryItem("uot")) uot = query.queryItemValue("uot") == "true" || query.queryItemValue("uot").toInt() > 0;
        
        if (server_port == 0) server_port = 1080;

        return !server.isEmpty();
    }

    bool socks::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty() || object["type"].toString() != "socks") return false;
        outbound::ParseFromJson(object);
        if (object.contains("username")) username = object["username"].toString();
        if (object.contains("password")) password = object["password"].toString();
        if (object.contains("version")) version = object["version"].toInt();
        if (object.contains("uot")) uot = object["uot"].toBool();
        phobos_key = object["phobos_key"].toString();
        phobos_masking = object["phobos_masking"].toString();
        phobos_media_ssrc = quint32(object["phobos_media_ssrc"].toInteger());
        return true;
    }

    bool socks::ParsePhobosConf(const QString& conf)
    {
        QString section, target;
        for (const auto& line : conf.split('\n')) {
            const auto trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith('#')) continue;
            if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
                section = trimmed.mid(1, trimmed.size() - 2).trimmed().toLower();
                continue;
            }
            const auto equals = trimmed.indexOf('=');
            if (equals <= 0) continue;
            const auto key = trimmed.left(equals).trimmed().toLower();
            const auto value = trimmed.mid(equals + 1).trimmed();
            if (value.compare("none", Qt::CaseInsensitive) == 0) continue;
            if (section == "instance") {
                if (key == "target") target = value;
                else if (key == "key") phobos_key = value;
                else if (key == "masking") phobos_masking = value.toUpper() == "AUTO" ? QString() : value.toUpper();
                else if (key == "media-ssrc") phobos_media_ssrc = value.toUInt(nullptr, 0);
            } else if (section == "socks5") {
                if (key == "login") username = value;
                else if (key == "password") password = value;
            }
        }
        const auto colon = target.lastIndexOf(':');
        if (colon <= 0 || phobos_key.isEmpty()) return false;
        server = target.left(colon).trimmed();
        if (server.startsWith('[') && server.endsWith(']')) server = server.mid(1, server.length() - 2);
        server_port = target.mid(colon + 1).toInt();
        version = 5;
        return !server.isEmpty() && server_port > 0 && server_port <= 65535;
    }

    bool socks::ParseFromClash(const clash::Proxies& object)
    {
        if (object.type != "socks5") return false;
        outbound::ParseFromClash(object);
        username = QString::fromStdString(object.username);
        password = QString::fromStdString(object.password);

        return true;
    }

    QString socks::ExportToLink()
    {
        if (!phobos_key.isEmpty()) {
            QStringList conf{"[instance]", "mode = socks5", "role = client", "source-if = 127.0.0.1", "source-lport = 1080",
                             "target = " + ::DisplayAddress(server, server_port), "key = " + phobos_key,
                             "masking = " + (phobos_masking.isEmpty() ? QStringLiteral("NONE") : phobos_masking)};
            if (phobos_media_ssrc > 0) conf << "media-ssrc = " + QString::number(phobos_media_ssrc);
            conf << "" << "[socks5]" << "login = " + (username.isEmpty() ? QStringLiteral("none") : username)
                 << "password = " + (password.isEmpty() ? QStringLiteral("none") : password);
            return "phobos://" + QString::fromLatin1((conf.join('\n') + '\n').toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)) +
                   "#" + (name.isEmpty() ? QStringLiteral("none") : QString::fromLatin1(QUrl::toPercentEncoding(name)));
        }
        QUrl url;
        QUrlQuery query;
        
        QString scheme = "socks5";
        if (version == 4) {
            scheme = "socks4";
        }
        url.setScheme(scheme);
        
        url.setHost(server);
        url.setPort(server_port);
        if (!name.isEmpty()) url.setFragment(name);
        
        if (!username.isEmpty()) url.setUserName(username);
        if (!password.isEmpty()) url.setPassword(password);
        if (uot) query.addQueryItem("uot", "1");
        
        mergeUrlQuery(query, outbound::ExportToLink());
        if (!query.isEmpty()) url.setQuery(query);
        return url.toString(QUrl::FullyEncoded);
    }

    QJsonObject socks::ExportToJson()
    {
        QJsonObject object;
        object["type"] = "socks";
        mergeJsonObjects(object, outbound::ExportToJson());
        if (!username.isEmpty()) object["username"] = username;
        if (!password.isEmpty()) object["password"] = password;
        if (version == 4) object["version"] = "4";
        if (uot) object["uot"] = uot;
        if (!phobos_key.isEmpty()) {
            object["phobos_key"] = phobos_key;
            if (!phobos_masking.isEmpty()) object["phobos_masking"] = phobos_masking;
            if (phobos_media_ssrc > 0) object["phobos_media_ssrc"] = qint64(phobos_media_ssrc);
        }
        return object;
    }

    BuildResult socks::Build()
    {
        QJsonObject object;
        if (!phobos_key.isEmpty()) {
            // The core's Phobos outbound: SOCKS5 inside the obfuscated stream, TCP only.
            object["type"] = "phobos";
            mergeJsonObjects(object, outbound::Build().object);
            if (!username.isEmpty()) object["username"] = username;
            if (!password.isEmpty()) object["password"] = password;
            object["key"] = phobos_key;
            if (!phobos_masking.isEmpty() && phobos_masking != "NONE") object["masking"] = phobos_masking.toLower();
            if (phobos_media_ssrc > 0) object["media_ssrc"] = qint64(phobos_media_ssrc);
            return {object, ""};
        }
        object["type"] = "socks";
        mergeJsonObjects(object, outbound::Build().object);
        if (!username.isEmpty()) object["username"] = username;
        if (!password.isEmpty()) object["password"] = password;
        if (version == 4) object["version"] = "4";
        if (uot) object["uot"] = uot;
        return {object, ""};
    }

    QString socks::DisplayType()
    {
        return phobos_key.isEmpty() ? "Socks" : "Phobos";
    }
}
