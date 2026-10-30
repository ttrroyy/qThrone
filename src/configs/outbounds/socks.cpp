#include "include/configs/outbounds/socks.h"

#include <QCoreApplication>
#include <QUrlQuery>
#include <include/global/Utils.hpp>

#include "include/configs/ParseDiagnostics.h"
#include "include/configs/common/utils.h"

namespace Configs {
    namespace {
        // Build() emits SOCKS5 for anything but 4, which is also the core's default.
        void parseSocksVersion(const QString& text, int& version, bool& socks4a)
        {
            const auto value = text.trimmed().toLower();
            socks4a = value == "4a";
            version = socks4a || value == "4" ? 4 : 5;
            if (!value.isEmpty() && version == 5 && value != "5") {
                ParseDiag::Warn("version", QCoreApplication::translate("Validation", "Unknown SOCKS version, so SOCKS5 was used. Use 4, 4a or 5."), text.trimmed());
            }
        }
    }

    namespace Checks {
        QString SocksAuth(int version, const QString &username, const QString &password)
        {
            if (password.isEmpty()) return {};
            if (version == 4) return QCoreApplication::translate("Validation", "SOCKS4 sends no password, so it is ignored.");
            if (username.isEmpty()) return QCoreApplication::translate("Validation", "Enter a user name too; without one the password is not sent.");
            return {};
        }
    }

    bool socks::ParseFromLink(const QString& link)
    {
        auto url = QUrl(link);
        if (!url.isValid()) return false;
        auto query = QUrlQuery(url.query());

        outbound::ParseFromLink(link);
        if (query.hasQueryItem("version"))
        {
            parseSocksVersion(query.queryItemValue("version"), version, socks4a);
        } else
        {
            if (url.scheme() == "socks4" || url.scheme() == "socks4a") version = 4;
            if (url.scheme() == "socks5") version = 5;
            socks4a = url.scheme() == "socks4a";
        }
        
        if (!url.password().isEmpty() || !url.userName().isEmpty()) {
            username = url.userName();
            password = url.password();
            
            // v2rayN base64-encodes "user:pass" into the username; a plain name like "user" is valid base64 too.
            if (password.isEmpty() && !username.isEmpty()) {
                QString decoded = DecodeB64IfValid(username);
                if (decoded.contains(':')) {
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
        if (object.contains("version")) parseSocksVersion(object["version"].toVariant().toString(), version, socks4a);
        if (object.contains("uot")) uot = object["uot"].toBool();
        phobos_key = object["phobos_key"].toString();
        phobos_masking = object["phobos_masking"].toString();
        phobos_media_ssrc = quint32(object["phobos_media_ssrc"].toInteger());
        if (object.contains("udp_over_tcp"))
        {
            if (object["udp_over_tcp"].isBool()) uot = object["udp_over_tcp"].toBool();
            if (object["udp_over_tcp"].isObject()) uot = object["udp_over_tcp"].toObject()["enabled"].toBool();
        }
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
        
        url.setScheme(version != 4 ? "socks5" : socks4a ? "socks4a" : "socks4");
        
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
        if (version == 4) object["version"] = socks4a ? "4a" : "4";
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
        const auto base = outbound::Build();
        if (!base.error.isEmpty()) return {{}, base.error};
        mergeJsonObjects(object, base.object);
        if (!username.isEmpty()) object["username"] = username;
        if (!password.isEmpty()) object["password"] = password;
        if (version == 4) object["version"] = socks4a ? "4a" : "4";
        if (uot) object["udp_over_tcp"] = true;
        return {object, ""};
    }

    QString socks::DisplayType()
    {
        return phobos_key.isEmpty() ? "Socks" : "Phobos";
    }

    void socks::Validate(Validation::Issues &issues)
    {
        outbound::Validate(issues);
        if (auto message = Checks::SocksAuth(version, username, password); !message.isEmpty())
            issues.append({Validation::Severity::Warning, "password", message});
    }
}
