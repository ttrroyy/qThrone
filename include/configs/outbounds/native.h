#pragma once
#include "include/configs/outbounds/extracore.h"

namespace Configs {
// Profiles run by a bundled helper behind an authenticated loopback SOCKS bridge.
bool IsNativeTransport(const QString &type);
// Native transports that carry TCP streams only; DNS through them must use TCP.
bool IsTcpOnlyTransport(const QString &type);
// Native transports without a server address of their own (a room, a domain, a bucket).
bool HasOwnServerFields(const QString &type);
// Prepare is specific to each transport; this picks the right one.
QString PrepareNativeTransport(extracore *outbound);
}
