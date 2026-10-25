#pragma once

#include "include/configs/common/Outbound.h"

namespace Configs
{
    class socks : public outbound
    {
        public:
        QString username;
        QString password;
        int version = 5;
        bool uot = false;

        // Phobos SOCKS5 mode: the SOCKS5 server sits behind the obfuscator's client role; empty key is plain SOCKS.
        QString phobos_key;
        // NONE, STUN, MEDIA or TLS.
        QString phobos_masking;
        quint32 phobos_media_ssrc = 0;

        // The [instance] (mode = socks5) and [socks5] sections of a Phobos .conf.
        bool ParsePhobosConf(const QString& conf);

        bool ParseFromLink(const QString& link) override;
        bool ParseFromJson(const QJsonObject& object) override;
        bool ParseFromClash(const clash::Proxies& object) override;
        QString ExportToLink() override;
        QJsonObject ExportToJson() override;
        BuildResult Build() override;

        QString DisplayType() override;
    };
}
