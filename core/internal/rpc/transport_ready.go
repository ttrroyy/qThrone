package rpc

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"net"
	"strconv"
	"time"
)

var errSOCKSUnauthenticated = errors.New("transport SOCKS listener accepted a client without authentication")
var errSOCKSRejected = errors.New("transport SOCKS listener rejected this launch's credentials")

// socksAuthenticate passes the SOCKS5 username/password negotiation and nothing more.
// A listener that offers "no authentication" is refused: every bundled helper must ask.
func socksAuthenticate(conn net.Conn, user, pass string) error {
	if len(user) < 1 || len(user) > 255 || len(pass) < 1 || len(pass) > 255 {
		return errors.New("transport SOCKS credentials required")
	}
	reply := make([]byte, 2)
	if _, err := conn.Write([]byte{5, 1, 2}); err != nil {
		return err
	}
	if _, err := io.ReadFull(conn, reply); err != nil {
		return err
	}
	if reply[0] != 5 || reply[1] == 0 {
		return errSOCKSUnauthenticated
	}
	if reply[1] != 2 {
		return errSOCKSRejected
	}
	request := append([]byte{1, byte(len(user))}, user...)
	request = append(append(request, byte(len(pass))), pass...)
	if _, err := conn.Write(request); err != nil {
		return err
	}
	if _, err := io.ReadFull(conn, reply); err != nil {
		return err
	}
	if reply[0] != 1 || reply[1] != 0 {
		return errSOCKSRejected
	}
	return nil
}

// The helper process existing is not proof of a usable tunnel. Every bundled
// helper opens its SOCKS listener only once its tunnel can carry traffic, and the
// listener counts as ready only once it accepts this launch's login and password.
func waitTransportReady(ctx context.Context, config string, done <-chan struct{}) error {
	var c struct {
		SOCKS   string `json:"socks"`
		User    string `json:"socks_user"`
		Pass    string `json:"socks_pass"`
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
	if kind, known := transportKinds[c.Backend]; known {
		budget = kind.connectStartup
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
			_ = conn.SetDeadline(time.Now().Add(2 * time.Second))
			err = socksAuthenticate(conn, c.User, c.Pass)
			conn.Close()
			if err == nil {
				return ctx.Err()
			}
			// A listener that answers wrongly is not a tunnel still starting up.
			if errors.Is(err, errSOCKSUnauthenticated) || errors.Is(err, errSOCKSRejected) || c.User == "" {
				return err
			}
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
