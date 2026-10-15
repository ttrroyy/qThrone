#include "include/configs/outbounds/custom.h"

#include "include/configs/common/OutboundFactory.h"
#include "include/configs/common/utils.h"
#include "include/configs/outbounds/xrayVless.h"
#include "include/global/Utils.hpp"

#include <QJsonArray>
#include <memory>

namespace Configs
{
    namespace
    {
        bool isSingBoxInfra(const QString& t)
        {
            return t == "direct" || t == "block" || t == "dns";
        }

        SecurityInfo analyzeSingBoxOutbound(const QJsonObject& o)
        {
            const auto type = o["type"].toString();
            if (type.isEmpty() || type == "custom" || type == "selector" || type == "urltest"
                || isSingBoxInfra(type))
                return {};
            std::shared_ptr<outbound> ob(NewOutboundByType(type));
            if (!ob || ob->invalid) return {};
            ob->ParseFromJson(o);
            return WithPrivateServer(ob->GetSecurity(), ob->GetAddress());
        }

        QJsonObject singBoxOutboundIdentity(const QJsonObject& o)
        {
            const auto type = o["type"].toString();
            if (type.isEmpty() || type == "custom" || type == "selector" || type == "urltest"
                || isSingBoxInfra(type))
                return {};
            std::shared_ptr<outbound> ob(NewOutboundByType(type));
            if (!ob || ob->invalid) return {};
            ob->ParseFromJson(o);
            return ob->ExportIdentity();
        }

        QJsonObject resolveSingBoxEgress(const QJsonArray& outbounds, const QString& tag, int depth)
        {
            if (depth <= 0 || outbounds.isEmpty()) return {};
            // The named tag, or the first outbound (sing-box's default egress).
            QJsonObject target;
            if (tag.isEmpty()) {
                target = outbounds.first().toObject();
            } else {
                for (const auto& v : outbounds) {
                    if (auto obj = v.toObject(); obj["tag"].toString() == tag) {
                        target = obj;
                        break;
                    }
                }
            }
            if (target.isEmpty()) return {};
            const auto type = target["type"].toString();
            if (type == "selector" || type == "urltest") {
                auto subs = target["outbounds"].toArray();
                QString next = target["default"].toString();
                if (next.isEmpty() && !subs.isEmpty()) next = subs.first().toString();
                return resolveSingBoxEgress(outbounds, next, depth - 1);
            }
            if (type.isEmpty() || isSingBoxInfra(type)) return {};
            return target;
        }

        bool isXrayInfra(const QString& p)
        {
            // direct and block are Xray's aliases of freedom and blackhole.
            return p == "freedom" || p == "direct" || p == "blackhole" || p == "block" || p == "dns" || p == "loopback";
        }

        // Xray lets a top-level address replace vnext/servers, taking the inline user with it.
        QJsonObject xrayPeer(const QJsonObject& settings)
        {
            if (settings.contains("address")) return settings;
            return (settings.contains("vnext") ? settings["vnext"] : settings["servers"]).toArray().first().toObject();
        }

        QString xrayVlessEncryption(const QJsonObject& settings)
        {
            const auto peer = xrayPeer(settings);
            const auto user = peer.contains("users") ? peer["users"].toArray().first().toObject() : peer;
            return user["encryption"].toString();
        }

        SecurityInfo xrayOutboundSecurity(const QJsonObject& o)
        {
            const auto protocol = o["protocol"].toString();
            if (protocol.isEmpty() || isXrayInfra(protocol)) return {};

            const auto stream = o["streamSettings"].toObject();
            SecurityInfo info;
            info.transport = DisplayTransportName(stream["network"].toString());

            const auto security = stream["security"].toString();
            if (security == "reality") {
                info.label = QObject::tr("Reality");
                info.level = SecurityLevel::Secure;
                return info;
            }
            if (security == "tls") {
                const bool insecure = stream["tlsSettings"].toObject()["allowInsecure"].toBool();
                info.label = insecure ? QObject::tr("Insecure TLS") : QObject::tr("TLS");
                info.level = insecure ? SecurityLevel::Weak : SecurityLevel::Secure;
                return info;
            }
            // Without transport security, shadowsocks, WireGuard and VLESS Encryption still encrypt; VMess counts as insecure.
            if (protocol == "shadowsocks" || protocol == "wireguard"
                || (protocol == "vless" && IsVlessEncrypted(xrayVlessEncryption(o["settings"].toObject())))) {
                info.label = QObject::tr("Encrypted");
                info.level = SecurityLevel::Secure;
                return info;
            }
            if (protocol == "vmess") {
                info.label = QObject::tr("Insecure");
                info.level = SecurityLevel::Weak;
                return info;
            }
            info.label = QObject::tr("Raw");
            info.level = SecurityLevel::None;
            return info;
        }

        SecurityInfo analyzeXrayOutbound(const QJsonObject& o)
        {
            return WithPrivateServer(xrayOutboundSecurity(o), xrayPeer(o["settings"].toObject()).value("address").toString());
        }

        QJsonObject xrayOutboundIdentity(const QJsonObject& o)
        {
            const auto protocol = o["protocol"].toString();
            if (protocol.isEmpty() || isXrayInfra(protocol)) return {};
            QJsonObject id;
            id["protocol"] = protocol;
            const auto settings = o["settings"].toObject();
            QJsonObject peer;
            if (settings.contains("vnext")) peer = settings["vnext"].toArray().first().toObject();
            else if (settings.contains("servers")) peer = settings["servers"].toArray().first().toObject();
            if (!peer.isEmpty()) {
                id["server"] = peer["address"];
                id["server_port"] = peer["port"];
            }
            const auto stream = o["streamSettings"].toObject();
            QJsonObject sid;
            if (const auto network = stream["network"].toString(); !network.isEmpty()) sid["network"] = network;
            const auto security = stream["security"].toString();
            if (!security.isEmpty()) sid["security"] = security;
            if (security == "reality") {
                const auto r = stream["realitySettings"].toObject();
                if (r.contains("serverName")) sid["sni"] = r["serverName"];
                if (r.contains("fingerprint")) sid["fingerprint"] = r["fingerprint"];
            } else if (security == "tls") {
                const auto t = stream["tlsSettings"].toObject();
                if (t.contains("serverName")) sid["sni"] = t["serverName"];
                if (t.contains("fingerprint")) sid["fingerprint"] = t["fingerprint"];
            }
            if (!sid.isEmpty()) id["stream"] = sid;
            return id;
        }

        // Xray routes to the first outbound by default; pick the first real proxy.
        QJsonObject firstXrayEgress(const QJsonArray& outbounds)
        {
            for (const auto& v : outbounds) {
                if (auto obj = v.toObject(); !isXrayInfra(obj["protocol"].toString())) return obj;
            }
            return {};
        }
    } // namespace

    QJsonObject Custom::DisplaySingBoxOutbound() const {
        const auto object = QString2QJsonObject(config);
        if (type != CustomFullConfig) return object;
        return resolveSingBoxEgress(object["outbounds"].toArray(), object["route"].toObject()["final"].toString(), 16);
    }

    QString Custom::DisplaySingBoxType() const {
        const auto object = DisplaySingBoxOutbound();
        auto protocol = object["type"].toString();
        if (protocol.isEmpty()) return type == CustomFullConfig ? "Custom Config" : "Custom Outbound";
        if (protocol != "custom") {
            std::shared_ptr<outbound> parsed(NewOutboundByType(protocol));
            if (parsed && !parsed->invalid) {
                parsed->ParseFromJson(object);
                const auto label = parsed->DisplayType();
                if (!label.isEmpty()) return label;
            }
        }
        protocol[0] = protocol[0].toUpper();
        return protocol;
    }

    QString Custom::DisplayXrayType() const {
        auto protocol = DisplayXrayOutbound()["protocol"].toString();
        if (protocol.isEmpty()) return "Custom Xray Config";
        if (protocol == "vless") return "VLESS (Xray)";
        if (protocol == "hysteria2" || protocol == "hy2") protocol = "hysteria";
        if (protocol != "custom") {
            std::shared_ptr<outbound> native(NewOutboundByType(protocol));
            if (native && !native->invalid) {
                const auto label = native->DisplayType();
                if (!label.isEmpty()) return label;
            }
        }
        protocol[0] = protocol[0].toUpper();
        return protocol;
    }

    QJsonObject Custom::DisplayXrayOutbound() const {
        const auto object = QString2QJsonObject(config);
        if (type != CustomXrayFullConfig) return object;
        const auto outbounds = object["outbounds"].toArray();
        const auto routing = object["routing"].toObject();
        for (const auto &value : routing["rules"].toArray()) {
            const auto rule = value.toObject();
            const auto tag = rule["outboundTag"].toString();
            for (const auto &outbound : outbounds) {
                const auto candidate = outbound.toObject();
                if (!tag.isEmpty() && candidate["tag"].toString() == tag && !isXrayInfra(candidate["protocol"].toString())) return candidate;
            }
            for (const auto &entry : routing["balancers"].toArray()) {
                const auto balancer = entry.toObject();
                if (rule["balancerTag"].toString().isEmpty() || rule["balancerTag"] != balancer["tag"]) continue;
                for (const auto &selector : balancer["selector"].toArray())
                    for (const auto &outbound : outbounds) {
                        const auto candidate = outbound.toObject();
                        if (candidate["tag"].toString().startsWith(selector.toString()) && !isXrayInfra(candidate["protocol"].toString())) return candidate;
                    }
            }
        }
        return firstXrayEgress(outbounds);
    }

    SecurityInfo Custom::GetSecurity()
    {
        if (type == CustomOutbound) {
            return analyzeSingBoxOutbound(QString2QJsonObject(config));
        }
        if (type == CustomFullConfig) {
            const auto obj = QString2QJsonObject(config);
            const auto egress = resolveSingBoxEgress(obj["outbounds"].toArray(),
                                                     obj["route"].toObject()["final"].toString(), 5);
            return egress.isEmpty() ? SecurityInfo{} : analyzeSingBoxOutbound(egress);
        }
        if (type == CustomXrayOutbound) {
            return analyzeXrayOutbound(QString2QJsonObject(config));
        }
        if (type == CustomXrayFullConfig) {
            const auto obj = QString2QJsonObject(config);
            const auto egress = firstXrayEgress(obj["outbounds"].toArray());
            return egress.isEmpty() ? SecurityInfo{} : analyzeXrayOutbound(egress);
        }
        return {};
    }

    QJsonObject Custom::ExportIdentity()
    {
        QJsonObject ob;
        if (type == CustomOutbound) {
            ob = singBoxOutboundIdentity(QString2QJsonObject(config));
        } else if (type == CustomFullConfig) {
            const auto obj = QString2QJsonObject(config);
            const auto egress = resolveSingBoxEgress(obj["outbounds"].toArray(),
                                                     obj["route"].toObject()["final"].toString(), 5);
            if (!egress.isEmpty()) ob = singBoxOutboundIdentity(egress);
        } else if (type == CustomXrayOutbound) {
            ob = xrayOutboundIdentity(QString2QJsonObject(config));
        } else if (type == CustomXrayFullConfig) {
            const auto obj = QString2QJsonObject(config);
            const auto egress = firstXrayEgress(obj["outbounds"].toArray());
            if (!egress.isEmpty()) ob = xrayOutboundIdentity(egress);
        }
        if (ob.isEmpty()) return ExportToJson();
        return QJsonObject{{"subtype", type}, {"ob", ob}};
    }
}
