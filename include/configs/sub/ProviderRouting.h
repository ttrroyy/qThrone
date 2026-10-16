#pragma once
#include "include/database/entities/Group.h"
#include "include/database/entities/RouteProfile.h"
#include <QJsonArray>

namespace Subscription::ProviderRouting {
QString Source(const QJsonObject &document, const QString &fallback = "Happ");
QString Identity(const QJsonObject &document, const QString &fallback = "Happ");
bool SameSettings(QJsonObject a, QJsonObject b);
struct Command {
    QString provider;
    bool activate = false;
    bool off = false;
    QJsonObject document;
};
struct Payload {
    QByteArray body;
    QList<Command> commands;
    QStringList warnings;
};
bool IsLink(const QString &input);
bool Parse(const QString &input, Command &command, QString &error, const QString &provider = "Happ");
Payload Extract(const QByteArray &body, const QString &header = {}, bool ignore = false);
void Apply(Configs::SubscriptionOptions &options, const QList<Command> &commands);
std::shared_ptr<Configs::RouteProfile> Profile(const QJsonObject &document, const QString &provider);
QString GeoUrl(const QJsonObject &document, const QString &kind);
QString PrepareGeo(const QJsonObject &document, const QString &kind, QString &path, bool force = false);
QString ResolveGeo(Configs::RouteProfile &profile, const QJsonObject &document);
QString ResolveXrayGeo(const QJsonObject &document, QJsonObject &config);
QString Dns(const QJsonObject &document, const QString &prefix, const QString &fallback);
QStringList Hosts(const QJsonObject &document);
bool Truth(const QJsonObject &document, const QString &key, bool fallback);
QJsonObject SourceDocument(const Configs::SubscriptionOptions &options);
} // namespace Subscription::ProviderRouting
