# qWDTT desktop adapter

Transport sources originate from SpaceNeuroX/proxy-turn-vk-android/go_client,
commit a296c57eaba69bb9479a24f9157856490890e47d. The upstream GPL license is retained.

qThrone invokes this executable with `-config <private temporary JSON file>` through
Throne's existing extra-core process lifecycle. `bridge_config.go` translates the
profile to upstream flags. RAW uses `raw_netstack.go` with the upstream IP packet
dispatcher; WG uses the upstream userspace WireGuard implementation. Both use
`bridge_socks.go` for authenticated SOCKS5 CONNECT and UDP ASSOCIATE over loopback.
No second operating-system TUN or installed WireGuard application is required.

The native traffic route is:

```text
Throne mixed inbound / Throne TUN
  -> sing-box SOCKS outbound
  -> qWDTT authenticated loopback bridge
  -> IPv4 netstack (RAW) / userspace WireGuard netstack (WG)
  -> upstream qWDTT TURN/RTP/AEAD transport -> compatible VPS
```

The Android TUN-FD CLI branch remains in upstream sources but is not invoked by
qThrone. Windows has an explicit unsupported stub for that Android-only operation.
Android WebView captcha fallback is disabled for desktop mode; original Go/RJS
automatic captcha solving remains. WireGuard private keys are no longer printed or
written to a local config file. Browser fingerprint state lives in the OS user's
private qThrone/qwdtt cache, isolated by device ID.

Tests require Go 1.26 or newer:

```bash
go test ./...
go test -race ./...
go vet ./...
```

Tests exercise TCP/UDP through the SOCKS bridge, actual raw IP stacks and two actual
userspace WireGuard devices. They do not authenticate to VK or provision a VPS.
See ../QTHRONE_GUIDE_RU.md for release setup and the limits of validation.
