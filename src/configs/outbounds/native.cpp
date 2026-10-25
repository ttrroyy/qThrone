#include "include/configs/outbounds/native.h"
#include "include/configs/outbounds/qwdtt.h"
#include "include/configs/outbounds/csqtt.h"
#include "include/configs/outbounds/wdttplus.h"
#include "include/configs/outbounds/openflux.h"
#include "include/configs/outbounds/olcrtc.h"
#include "include/configs/outbounds/dnstunnel.h"
#include "include/configs/outbounds/s3bypass.h"
#include "include/configs/outbounds/sstp.h"
#include <QSet>

namespace Configs {
bool IsNativeTransport(const QString &type) {
    static const QSet<QString> types{"qwdtt", "csqtt", "wdttplus", "openflux", "olcrtc", "dnstt", "vaydns", "slipstream", "masterdns", "cottendns", "soar", "s3bypass", "sstp"};
    return types.contains(type);
}

bool IsTcpOnlyTransport(const QString &type) {
    static const QSet<QString> types{"olcrtc", "dnstt", "vaydns", "slipstream", "masterdns", "cottendns", "soar"};
    return types.contains(type);
}

bool HasOwnServerFields(const QString &type) {
    static const QSet<QString> types{"olcrtc", "dnstt", "vaydns", "slipstream", "masterdns", "cottendns", "soar", "s3bypass"};
    return types.contains(type);
}

QString PrepareNativeTransport(extracore *outbound) {
    if (auto *o = dynamic_cast<csqtt *>(outbound)) return o->Prepare();
    if (auto *o = dynamic_cast<wdttplus *>(outbound)) return o->Prepare();
    if (auto *o = dynamic_cast<qwdtt *>(outbound)) return o->Prepare();
    if (auto *o = dynamic_cast<openflux *>(outbound)) return o->Prepare();
    if (auto *o = dynamic_cast<olcrtc *>(outbound)) return o->Prepare();
    if (auto *o = dynamic_cast<dnstunnel *>(outbound)) return o->Prepare();
    if (auto *o = dynamic_cast<s3bypass *>(outbound)) return o->Prepare();
    if (auto *o = dynamic_cast<sstp *>(outbound)) return o->Prepare();
    return {};
}
}
