package main

import (
	"context"
	"errors"
	"fmt"
	"net"
	"os"
	"time"
)

func csqttDialTiming(stage string, started time.Time, err error) {
	if os.Getenv("CSQTT_DIAGNOSTICS") == "1" {
		fmt.Printf("[CSQTT diagnostic] stage=%s ms=%d success=%t\n", stage, time.Since(started).Milliseconds(), err == nil)
	}
}

// Resolve inside the CSQTT IP tunnel. TCP DNS survives a lost initial datagram
// without waiting for the netstack's UDP DNS retry beyond the URL-test budget.
func csqttTunnelDial(dial bridgeDial, dns []string) bridgeDial {
	resolver := &net.Resolver{PreferGo: true, Dial: func(ctx context.Context, _, _ string) (net.Conn, error) {
		var last error
		for _, server := range dns {
			started := time.Now()
			conn, err := dial(ctx, "tcp", net.JoinHostPort(server, "53"))
			csqttDialTiming("dns-connect", started, err)
			if err == nil {
				return conn, nil
			}
			last = err
		}
		if last == nil {
			last = errors.New("CSQTT tunnel has no DNS server")
		}
		return nil, last
	}}
	return func(ctx context.Context, network, address string) (net.Conn, error) {
		host, port, err := net.SplitHostPort(address)
		if err != nil || net.ParseIP(host) != nil {
			return dial(ctx, network, address)
		}
		started := time.Now()
		addresses, err := resolver.LookupIP(ctx, "ip4", host)
		csqttDialTiming("dns-lookup", started, err)
		if err != nil {
			return nil, err
		}
		for _, ip := range addresses {
			started := time.Now()
			conn, e := dial(ctx, network, net.JoinHostPort(ip.String(), port))
			csqttDialTiming("target-connect", started, e)
			if e == nil {
				return conn, nil
			}
			err = e
		}
		if err == nil {
			err = errors.New("CSQTT DNS returned no IPv4 address")
		}
		return nil, err
	}
}
