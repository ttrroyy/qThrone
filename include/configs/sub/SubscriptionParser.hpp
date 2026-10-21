#pragma once

#include "include/database/entities/Profile.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace Subscription {
    QString UnwrapProviderLink(const QString &text);
    QStringList CsqttSubscriptionLinks(const QByteArray &body);
    struct ParseSink {
        std::function<void(std::shared_ptr<Configs::Profile>)> profile;
        std::function<void(const QString &)> log;
        std::function<void(const QString &, const QString &)> warn;
    };

    // Profiles are handed to the sink in document order; the body is consumed in place.
    void ParseDocument(QByteArray body, const ParseSink &sink);

    void ParseText(const QString &text, const ParseSink &sink);
}
