# sing-trusttunnel

[![Go Reference](https://pkg.go.dev/badge/github.com/xchacha20-poly1305/sing-trusttunnel.svg)](https://pkg.go.dev/github.com/xchacha20-poly1305/sing-trusttunnel)

A sing style [TrustTunnel](https://trusttunnel.org/) implementation.

API reference: [sing-trusttunnel on pkg.go.dev](https://pkg.go.dev/github.com/xchacha20-poly1305/sing-trusttunnel)

## CLI

A minimum implementation

### Build

```bash
make build
```

### Usage

```
sing-trusttunnel [options] <command> [arguments]

Options:
  -v              Show version
  -c <path>       Config file path (default: config.json)

Commands:
  client          Run as client mode
  server          Run as server mode
  config-to-url   Convert client config to TrustTunnel URL
  url-to-config   Convert TrustTunnel URL to config
```

### Config

**Server**

```json
{
  "listen": "::",
  "listenPort": 443,
  "cert": "/path/to/cert.pem",
  "key": "/path/to/key.key",
  "serverName": "example.com",
  "users": [
    {
      "username": "trust",
      "password": "tunnel"
    }
  ],
  "listenQuic": true
}
```

**Client**

```json5
{
  "server": "server.example.com",
  "serverPort": 443,
  "username": "trust",
  "password": "tunnel",
  "serverName": "example.com",
  "allowInsecure": true,
  "healthCheck": true,
  "quic": true,
  
  // Socks listen
  "listen": "127.0.0.1",
  "listenPort": 1080,
  "auth": [
    {
      "username": "socks",
      "password": "5"
    }
  ]
}
```