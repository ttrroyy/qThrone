# qThrone 1.6

CSQTT builds only the original 2.5.0 Legacy client at
`ttrroyy/csqtt-rebirth@2066156119a5afe7d63e104aefdc8e0849999d03`,
the commit behind `v2.5.0-legacy`. Deployment uses that release's universal APK,
verified by SHA-256. No 2.1.9 or pre-release transport fallback is bundled.
Compatible profile/link syntax remains readable; it always runs the Legacy client.
The Rust patch changes private configuration I/O and desktop socket cfg only.
Passwords and account tokens stay out of argv; every launch gets a fresh epoch/salt.
Probes and live connections share a password-based session gate for Legacy.

WDTT Plus keeps its pinned original v20 native client, numbered captcha protocol,
START_CONFIG stdin, installer, administrative API and preserving update/rollback.
The editor now exposes its TCP-only, external SNI, TLS fingerprint/client IDs,
preflight and WARP MASQUE settings. Optional first enrollment uses only an
unambiguous matching saved VPS with an approved SSH host key. The relay reaches
only api.cloudflareclient.com:443; TLS remains end to end. A failed SSH bootstrap
falls back to direct enrollment, as on mobile.

The qWDTT cookie binding supplies all three protocols' Auto hashes and the
launch-only token for CSQTT Auto JS. Link-provided tokens take precedence.
Token reuse, HTTP-to-browser fallback, one auth-error refresh and rate limits
are shared. An account token obtained for a launch is not saved in the profile.

Subscription refresh preserves hash source, worker count, device identity and
manually selected protocol settings. Workers never increase just because more
hashes arrive; they clamp only to the protocol/server limit. Manually edited
hash lists have an explicit persistence marker. Older saved profiles without
that marker are conservatively treated as manual, so their hashes are not lost.
Unedited newly imported lists still accept the provider's leading hash slots.
Ambiguous matches do not inherit another profile's choices.

Imports use the common file/clipboard/subscription parser: MASQUE URIs preserve
percent-encoded key bytes, IPv4/IPv6, HTTP version, MTU and SNI; Clash YAML resolves
mapping/sequence merges with depth and expansion limits and imports WireGuard,
AWG options and MASQUE. SSConnect/Happ wrappers unwrap without fetching landing
pages. CSQTT QR HTML is read only for explicit csqtt:// input/link attributes.

SSTP uses the mobile client's userspace PPP/MS-CHAPv2/TLS stack, with an
authenticated loopback SOCKS bridge and parent-owned process lifetime. Desktop
imports URI/JSON through the shared file, clipboard and subscription parser;
the editor exposes credentials, SNI and certificate validation.

HWID is enabled by default, retaining existing explicit preferences. Native
device IDs have a persistent per-user fallback. A provider's HWID placeholder
triggers at most one retry with headers and an HWID cookie, unless the group
explicitly opted out. A rejected placeholder never becomes a fake local profile.

Latency tests can overlap for distinct profiles; duplicate in-flight IDs are
skipped. RPC results belong to their requesting batch, preventing cross-run
result consumption. Automatic callers wait for overlapping IDs. Speed tests
remain exclusive. Ordinary latency results update when their RPC batch returns.
GeoData completion uses a five-second notification beside the restart notice.

VK login completion rejects login/authorization pages even when cookies exist.
Browser fallback runs only once per operation, retaining qWDTT's original cookie
binding and refresh flow. Unlink resets hash sources without revalidating an
unrelated incomplete editable profile. Multiline YAML mapping anchors now bind
to the full mapping instead of its first key. The macOS ARM build compiles its
code generator for the build host. A successful normal release promotes the
verified SHA to stable with a fast-forward push, refusing if dev moved meanwhile.

## Mobile audit (read-only)

Inspected qThroneMobile 2.4.4's NativeHashPolicy.preserve and its refresh caller.
The mobile source currently lets provider hashes replace leading manual slots;
qWDTT can raise a former maximum when the hash limit grows. It preserves qWDTT
mode and CSQTT TURN transport, but does not preserve every extra WDTT Plus/SRTP
option. No mobile source was changed. This was a source audit, not a phone test.

## Validation

Local result: 60 helper tests and 96 core RPC/relay/parent-policy tests passed;
Go vet passed for those packages. The helper share codec also passed an actual
stdin/stdout round trip. Local Go tests cover native adapters, authenticated SOCKS, captcha/lifecycle,
Legacy encrypted shares/negotiation, VK refresh/fallback and trusted bootstrap
selection. Core RPC tests cover live-session/probe separation. C++ self-tests
cover repeated subscription refresh/serialization, MASQUE, Clash aliases and
encrypted HTML subscriptions. The release workflow builds the desktop targets
and runs the packaged C++ self-tests plus the Windows upgrade check.
Live VK/VPN/SSH deployment and visual desktop operation still need runtime
verification; no long UI suite was run locally. SSTP's 88 protocol/SOCKS and
private-configuration tests pass locally, as do standalone C++ YAML-anchor and
concurrent-test ownership checks and the exact Clash merge fixture. The original
ClashWARP_95.yaml attachment is no longer available at its supplied path.
