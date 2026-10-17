# sing-trusttunnel for qThrone

Source: github.com/xchacha20-poly1305/sing-trusttunnel v0.3.2
(go.sum h1:3rj5AgkB1GGaDJ4u6iSa+1kQOpnxtw/GZ7nXjZPP1gc=), GPL license retained.

qThrone changes, shared with qThroneMobile (core-patches/trusttunnel):

- `https_fallback.go`: QUIC profiles race their HTTP/2 fallback the way
  TrustTunnelClient's FallbackableUpstreamConnector does. QUIC starts first,
  HTTP/2 joins after one second or as soon as QUIC fails, the first transport
  to answer carries the requests, and one that stops answering makes the next
  request race again. URL tests go through the same transport.
- `client.go`: `NewClient` enables the fallback for QUIC profiles.
