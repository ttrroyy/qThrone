package main

import (
	"context"
	"fmt"
	"net"
	"time"
)

const desktopPingAuthBudget = 90 * time.Second

var desktopPingTimeout time.Duration

// VK authentication is preparation, not the measured TURN/DTLS round trip.
// Once credentials are available, all relay attempts share the short probe budget.
func runPingProbe(ctx context.Context, hashes []string, authTimeout, probeTimeout time.Duration,
	fetch func(context.Context, string, int) (string, string, []string, error),
	probe func(context.Context, *Credentials) (int64, error)) (int64, error) {
	authCtx, cancelAuth := context.WithTimeout(ctx, authTimeout)
	defer cancelAuth()
	active := authCtx
	probeStarted := false
	var last error
	for i, hash := range hashes {
		if err := active.Err(); err != nil {
			if !probeStarted {
				return 0, fmt.Errorf("VK authorization: %w", err)
			}
			return 0, fmt.Errorf("TURN/DTLS: %w", err)
		}
		stream := 999 + i*streamsPerCache
		user, password, urls, err := fetch(active, hash, stream)
		if err != nil {
			last = fmt.Errorf("VK authorization: %w", err)
			continue
		}
		if !probeStarted {
			probeCtx, cancelProbe := context.WithTimeout(ctx, probeTimeout)
			defer cancelProbe()
			active = probeCtx
			probeStarted = true
		}
		delay, err := probe(active, &Credentials{User: user, Pass: password, TurnURLs: urls, CacheStreamID: stream})
		if err == nil {
			return delay, nil
		}
		last = fmt.Errorf("TURN/DTLS: %w", err)
	}
	if last == nil {
		last = fmt.Errorf("no usable VK hashes")
	}
	return 0, last
}

func measureNativePing(ctx context.Context, tp *TurnParams, peer *net.UDPAddr, hashes []string) (int64, error) {
	timeout := 15 * time.Second
	authTimeout := 90 * time.Second
	if desktopProbeOnly {
		authTimeout = desktopPingAuthBudget
	}
	if desktopPingTimeout > 0 {
		timeout = desktopPingTimeout
	}
	return runPingProbe(ctx, hashes, authTimeout, timeout, GetCreds, func(c context.Context, credentials *Credentials) (int64, error) {
		return RunPing(c, tp, peer, credentials)
	})
}
