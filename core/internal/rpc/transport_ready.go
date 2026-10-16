package rpc

import (
	"context"
	"encoding/json"
	"errors"
	"net"
	"strconv"
	"time"
)

// The helper process existing is not proof of a usable RAW/WG tunnel.
// Its SOCKS listener is created only after native configuration has arrived.
func waitTransportReady(ctx context.Context, config string, done <-chan struct{}) error {
	var c struct {
		SOCKS   string `json:"socks"`
		Backend string `json:"backend"`
	}
	if json.Unmarshal([]byte(config), &c) != nil {
		return errors.New("invalid transport startup configuration")
	}
	if c.Backend != "" && c.Backend != "csqtt" {
		return nil
	}
	host, port, err := net.SplitHostPort(c.SOCKS)
	n, _ := strconv.Atoi(port)
	if err != nil || host != "127.0.0.1" || n < 1 || n > 65535 {
		return errors.New("invalid transport startup listener")
	}
	ctx, cancel := context.WithTimeout(ctx, 5*time.Minute)
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
