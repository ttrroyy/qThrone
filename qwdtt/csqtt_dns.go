package main

import (
	"context"
	"errors"
	"net"
)

// Resolve inside the CSQTT IP tunnel. TCP DNS survives a lost initial datagram
// without waiting for the netstack's UDP DNS retry beyond the URL-test budget.
func csqttTunnelDial(dial bridgeDial, dns []string) bridgeDial {
	resolver := &net.Resolver{PreferGo: true, Dial: func(ctx context.Context, _, _ string) (net.Conn, error) {
		var last error
		for _, server := range dns {
			conn, err := dial(ctx, "tcp", net.JoinHostPort(server, "53"))
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
		addresses, err := resolver.LookupIP(ctx, "ip4", host)
		if err != nil {
			return nil, err
		}
		for _, ip := range addresses {
			conn, e := dial(ctx, network, net.JoinHostPort(ip.String(), port))
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
