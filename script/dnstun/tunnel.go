package main

import (
	"context"
	"errors"
	"fmt"
	"log"
	"net"
	"net/url"
	"strings"
	"sync"
	"time"

	"github.com/net2share/vaydns/client"
)

// utlsDistribution is vaydns-client's default fingerprint mix for DoH/DoT.
const utlsDistribution = "4*random,3*Firefox_120,1*Firefox_105,3*Chrome_120,1*Chrome_102,1*iOS_14,1*iOS_13"

// parseResolver reads the resolver forms the apps share: "1.1.1.1",
// "1.1.1.1:53", "udp://1.1.1.1:53", "https://dns.example/dns-query" (DoH)
// and "tls://dns.example:853" (DoT).
func parseResolver(spec string) (client.Resolver, error) {
	spec = strings.TrimSpace(spec)
	switch {
	case strings.HasPrefix(spec, "https://"):
		if _, err := url.Parse(spec); err != nil {
			return client.Resolver{}, err
		}
		return client.NewResolver(client.ResolverTypeDOH, spec)
	case strings.HasPrefix(spec, "tls://"):
		return client.NewResolver(client.ResolverTypeDOT, withPort(strings.TrimPrefix(spec, "tls://"), "853"))
	default:
		return client.NewResolver(client.ResolverTypeUDP, withPort(strings.TrimPrefix(spec, "udp://"), "53"))
	}
}

func withPort(host, port string) string {
	if _, _, err := net.SplitHostPort(host); err == nil {
		return host
	}
	return net.JoinHostPort(strings.Trim(host, "[]"), port)
}

// dnsTunnel keeps one KCP/Noise/smux session through a resolver and rebuilds
// it, moving to the next resolver after repeated failures, when it breaks.
type dnsTunnel struct {
	cfg       config
	resolvers []string
	mu        sync.Mutex
	tunnel    *client.Tunnel
	current   int
	failures  int
	closed    bool
}

func newDNSTunnel(c config) (*dnsTunnel, error) {
	t := &dnsTunnel{cfg: c, resolvers: c.resolverList()}
	if _, err := t.build(); err != nil {
		return nil, err
	}
	return t, nil
}

func duration(value string) time.Duration {
	if d, err := time.ParseDuration(strings.TrimSpace(value)); err == nil && d > 0 {
		return d
	}
	return 0
}

// build validates the profile and makes a tunnel for the current resolver.
func (t *dnsTunnel) build() (*client.Tunnel, error) {
	resolver, err := parseResolver(t.resolvers[t.current%len(t.resolvers)])
	if err != nil {
		return nil, fmt.Errorf("resolver: %w", err)
	}
	if resolver.ResolverType != client.ResolverTypeUDP {
		if id, err := client.SampleUTLSDistribution(utlsDistribution); err == nil {
			resolver.UTLSClientHelloID = id
		}
	}
	server, err := client.NewTunnelServer(t.cfg.Domain, t.cfg.Pubkey)
	if err != nil {
		return nil, err
	}
	// DNSTT is VayDNS's wire compatibility mode (8-byte ClientID, dnstt padding).
	server.DnsttCompat = t.cfg.Type == "dnstt" || t.cfg.DnsttCompat
	server.ClientIDSize = t.cfg.ClientIDSize
	server.RecordType = t.cfg.RecordType
	tunnel, err := client.NewTunnel(resolver, server)
	if err != nil {
		return nil, err
	}
	tunnel.IdleTimeout = duration(t.cfg.IdleTimeout)
	tunnel.KeepAlive = duration(t.cfg.Keepalive)
	return tunnel, nil
}

func (t *dnsTunnel) connect() (*client.Tunnel, error) {
	tunnel, err := t.build()
	if err != nil {
		return nil, err
	}
	steps := []func() error{
		tunnel.InitiateResolverConnection,
		func() error { return tunnel.InitiateDNSPacketConn(tunnel.TunnelServer.Addr) },
		func() error { return tunnel.InitiateKCPConn(0) },
		tunnel.InitiateNoiseChannel,
		tunnel.InitiateSmuxSession,
	}
	for _, step := range steps {
		if err := step(); err != nil {
			_ = tunnel.Close()
			return nil, err
		}
	}
	return tunnel, nil
}

func (t *dnsTunnel) Open(ctx context.Context) (net.Conn, error) {
	for attempt := 0; attempt < 2; attempt++ {
		t.mu.Lock()
		if t.closed {
			t.mu.Unlock()
			return nil, net.ErrClosed
		}
		if t.tunnel == nil {
			tunnel, err := t.connect()
			if err != nil {
				t.failures++
				if t.failures >= 3 && len(t.resolvers) > 1 {
					t.current++
					t.failures = 0
					log.Printf("[DNS] switching to resolver %d of %d", t.current%len(t.resolvers)+1, len(t.resolvers))
				}
				t.mu.Unlock()
				return nil, fmt.Errorf("session: %w", err)
			}
			t.tunnel = tunnel
			log.Printf("[DNS] session established")
		}
		tunnel := t.tunnel
		t.mu.Unlock()

		stream, err := tunnel.OpenStream()
		if err == nil {
			t.mu.Lock()
			t.failures = 0
			t.mu.Unlock()
			return stream, nil
		}
		if ctx.Err() != nil {
			return nil, ctx.Err()
		}
		// A stream that cannot open means the session is gone: rebuild it once.
		log.Printf("[DNS] session lost: %v", err)
		t.mu.Lock()
		if t.tunnel == tunnel {
			_ = tunnel.Close()
			t.tunnel = nil
		}
		t.mu.Unlock()
	}
	return nil, errors.New("session could not be rebuilt")
}

func (t *dnsTunnel) Close() error {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.closed = true
	if t.tunnel != nil {
		return t.tunnel.Close()
	}
	return nil
}
