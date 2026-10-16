package rpc

import (
	"context"
	"encoding/json"
	"errors"
	"net"
	"strconv"
	"time"
)

// The helper process existing is not proof of a usable tunnel. Every bundled
// helper opens its SOCKS listener only once its tunnel can carry traffic.
func waitTransportReady(ctx context.Context, config string, done <-chan struct{}) error {
	var c struct {
		SOCKS   string `json:"socks"`
		Backend string `json:"backend"`
	}
	if json.Unmarshal([]byte(config), &c) != nil {
		return errors.New("invalid transport startup configuration")
	}
	host, port, err := net.SplitHostPort(c.SOCKS)
	n, _ := strconv.Atoi(port)
	if err != nil || host != "127.0.0.1" || n < 1 || n > 65535 {
		return errors.New("invalid transport startup listener")
	}
	// VK authorization may include captcha checks; rooms and documents answer quickly or not at all.
	budget := 5 * time.Minute
	if c.Backend == "openflux" || c.Backend == "olcrtc" {
		budget = 2 * time.Minute
	}
	ctx, cancel := context.WithTimeout(ctx, budget)
	defer cancel()
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()
	for {
		if err := ctx.Err(); err != nil {
			return err
		}
		select {
		case <-done:
			return errors.New("transport exited before its SOCKS bridge was ready")
		default:
		}
		conn, err := (&net.Dialer{Timeout: 100 * time.Millisecond}).DialContext(ctx, "tcp", c.SOCKS)
		if err == nil {
			conn.Close()
			return ctx.Err()
		}
		select {
		case <-done:
			return errors.New("transport exited before its SOCKS bridge was ready")
		case <-ctx.Done():
			return ctx.Err()
		case <-ticker.C:
		}
	}
}
