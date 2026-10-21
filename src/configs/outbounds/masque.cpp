#include "include/configs/outbounds/masque.h"

#include <QJsonArray>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <include/global/Utils.hpp>

#include "include/configs/common/utils.h"

namespace Configs {
    bool masque::ParseFromLink(const QString& link)
    {
        const QUrl url(link.trimmed());
        if (!url.isValid() || url.scheme() != "masque" || url.host().isEmpty() ||
            url.port(443) < 1 || url.port(443) > 65535 || !url.password().isEmpty() ||
            (!url.path().isEmpty() && url.path() != "/")) return false;
        const QUrlQuery query(url);
        QSet<QString> seen;
        for (const auto& item : query.queryItems()) {
            if (seen.contains(item.first)) return false;
            seen.insert(item.first);
        }
        const auto value = [&](const char* key) { return query.queryItemValue(key, QUrl::FullyDecoded); };
        const QRegularExpression keyPattern("^[A-Za-z0-9+/_=-]{1,8192}$");
        private_key = url.userName(QUrl::FullyDecoded);
        peer_public_key = value("publickey");
        if (!keyPattern.match(private_key).hasMatch() ||
            (!peer_public_key.isEmpty() && !keyPattern.match(peer_public_key).hasMatch())) return false;
        address.clear();
        for (auto raw : value("address").split(',')) {
            raw = raw.trimmed();
            const QHostAddress ip(raw.section('/', 0, 0));
            if (ip.isNull()) return false;
            const int bits = ip.protocol() == QAbstractSocket::IPv6Protocol ? 128 : 32;
            if (raw.contains('/')) {
                bool ok;
                const int prefix = raw.section('/', 1).toInt(&ok);
                if (!ok || prefix < 0 || prefix > bits) return false;
            } else raw += "/" + QString::number(bits);
            address << raw;
        }
        mtu = 1280;
        if (query.hasQueryItem("mtu")) {
            bool ok; mtu = value("mtu").toInt(&ok);
            if (!ok || mtu < 576 || mtu > 65535) return false;
        }
        http_version = 0;
        if (query.hasQueryItem("http_version")) {
            bool ok; http_version = value("http_version").toInt(&ok);
            if (!ok) return false;
        } else if (value("network") == "h2") http_version = 2;
        else if (value("network") == "h3") http_version = 3;
        else if (!value("network").isEmpty()) return false;
        if (http_version != 0 && http_version != 2 && http_version != 3) return false;
        disable_version_fallback = http_version == 3;
        if (query.hasQueryItem("disable_version_fallback")) {
            const auto flag = value("disable_version_fallback");
            if (flag != "true" && flag != "false" && flag != "1" && flag != "0") return false;
            disable_version_fallback = flag == "true" || flag == "1";
        }
        outbound::ParseFromLink(link);
        server_port = url.port(443);
        tls->ParseFromLink(link); tls->enabled = true;
        quic->ParseFromLink(link);
        return true;
    }
    static QString withPrefix(const std::string& address, const char* prefix)
    {
        auto result = QString::fromStdString(address).trimmed();
        if (!result.isEmpty() && !result.contains('/')) result += prefix;
        return result;
    }

    bool masque::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty() || object["type"].toString() != "masque") return false;
        outbound::ParseFromJson(object);
        if (object.contains("private_key")) private_key = object["private_key"].toString();
        if (object.contains("peer_public_key")) peer_public_key = object["peer_public_key"].toString();
        // Listable: the core accepts a bare string as well as an array.
        if (object["address"].isArray()) address = QJsonArray2QListString(object["address"].toArray());
        else if (object.contains("address")) address = {object["address"].toString()};
        if (object.contains("mtu")) mtu = object["mtu"].toInt();
        if (object.contains("http_version")) http_version = object["http_version"].toInt();
        if (object.contains("disable_version_fallback")) disable_version_fallback = object["disable_version_fallback"].toBool();
        if (object.contains("tls")) {
            auto tlsObject = object["tls"].toObject();
            tls->ParseFromJson(tlsObject);
            // An absent SNI means the server address (as in sing-box), not the constructor's WARP default.
            if (!tlsObject.contains("server_name")) tls->server_name.clear();
        }
        tls->enabled = true;
        quic->ParseFromJson(object);
        return true;
    }

    bool masque::ParseFromClash(const clash::Proxies& object)
    {
        if (object.type != "masque") return false;
        outbound::ParseFromClash(object);
        if (server_port == 0) server_port = 443;

        private_key = QString::fromStdString(object.private_key);
        peer_public_key = QString::fromStdString(object.public_key);
        if (auto v4 = withPrefix(object.ip, "/32"); !v4.isEmpty()) address << v4;
        if (auto v6 = withPrefix(object.ipv6, "/128"); !v6.isEmpty()) address << v6;
        if (object.mtu > 0) mtu = object.mtu;

        if (!object.sni.empty()) tls->server_name = QString::fromStdString(object.sni);
        tls->insecure = object.skip_cert_verify;
        tls->enabled = true;

        // mihomo never falls back between carriers.
        if (object.network == "h2") {
            http_version = 2;
            disable_version_fallback = false;
        } else {
            http_version = 3;
            disable_version_fallback = true;
        }
        return !private_key.isEmpty();
    }

    QString masque::ExportToLink()
    {
        QUrl url; QUrlQuery query;
        url.setScheme("masque"); url.setHost(server); url.setPort(server_port);
        url.setUserName(private_key); url.setFragment(name);
        if (!peer_public_key.isEmpty()) query.addQueryItem("publickey", peer_public_key);
        query.addQueryItem("address", address.join(','));
        query.addQueryItem("mtu", QString::number(mtu));
        query.addQueryItem("http_version", QString::number(http_version));
        query.addQueryItem("disable_version_fallback", disable_version_fallback ? "true" : "false");
        mergeUrlQuery(query, outbound::ExportToLink());
        mergeUrlQuery(query, tls->ExportToLink());
        mergeUrlQuery(query, quic->ExportToLink());
        url.setQuery(query);
        return url.toString(QUrl::FullyEncoded).replace('+', "%2B");
    }

    QJsonObject masque::ExportToJson()
    {
        QJsonObject object;
        object["type"] = "masque";
        mergeJsonObjects(object, outbound::ExportToJson());
        if (!private_key.isEmpty()) object["private_key"] = private_key;
        if (!peer_public_key.isEmpty()) object["peer_public_key"] = peer_public_key;
        if (!address.isEmpty()) object["address"] = QListStr2QJsonArray(address);
        if (mtu > 0) object["mtu"] = mtu;
        if (http_version != 0) object["http_version"] = http_version;
        if (disable_version_fallback) object["disable_version_fallback"] = true;
        object["tls"] = tls->ExportToJson();
        mergeJsonObjects(object, quic->ExportToJson());
        return object;
    }

    QJsonObject masque::ExportIdentity()
    {
        // WARP identities share one server address; the key is what tells them apart.
        auto object = outbound::ExportIdentity();
        object["private_key"] = private_key;
        return object;
    }

    BuildResult masque::Build()
    {
        if (private_key.isEmpty()) return {{}, QObject::tr("%1: the private key is empty").arg(DisplayTypeAndName())};
        if (address.isEmpty()) return {{}, QObject::tr("%1: no address is set").arg(DisplayTypeAndName())};

        tls->enabled = true;
        QJsonObject object;
        object["type"] = "masque";
        if (!name.isEmpty()) object["tag"] = name;
        mergeJsonObjects(object, outbound::Build().object);
        object["private_key"] = private_key;
        if (!peer_public_key.isEmpty()) object["peer_public_key"] = peer_public_key;
        object["address"] = QListStr2QJsonArray(address);
        if (mtu > 0) object["mtu"] = mtu;
        if (http_version != 0) object["http_version"] = http_version;
        if (disable_version_fallback) object["disable_version_fallback"] = true;
        auto tlsObject = tls->Build().object;
        tlsObject.remove("reality");
        object["tls"] = tlsObject;
        mergeJsonObjects(object, quic->Build().object);
        return {object, ""};
    }

    QString masque::DisplayType()
    {
        return "MASQUE";
    }

    SecurityInfo masque::GetSecurity()
    {
        // The peer key pins the server certificate, which the core enforces even when insecure is set.
        if (!peer_public_key.isEmpty()) return {QObject::tr("TLS"), {}, SecurityLevel::Secure};
        return SecurityFromTLS({});
    }

    bool masque::IsEndpoint()
    {
        return true;
    }
}
