package main

import (
	"context"
	"encoding/base64"
	"errors"
	"fmt"
	"net"
	"strings"
	"sync"
	"time"

	"github.com/shadowsocks/go-shadowsocks2/core"
	"github.com/shadowsocks/go-shadowsocks2/socks"
	"golang.org/x/crypto/ssh"
	"golang.org/x/net/proxy"
)

// backend turns tunnel streams into connections to targets, speaking the
// protocol of the server's backend.
type backend interface {
	Dial(ctx context.Context, target string) (net.Conn, error)
	// Check proves the tunnel and the backend answer.
	Check(ctx context.Context) error
}

func newBackend(c backendConfig, source streamSource) (backend, error) {
	switch c.Type {
	case "", "socks":
		return &socksBackend{source: source, user: c.User, pass: c.Password}, nil
	case "ssh":
		config := &ssh.ClientConfig{
			User:            c.User,
			HostKeyCallback: ssh.InsecureIgnoreHostKey(), // the tunnel itself is authenticated (Noise/TLS)
			Timeout:         30 * time.Second,
		}
		if strings.TrimSpace(c.PK) != "" {
			signer, err := ssh.ParsePrivateKey([]byte(pemKey(c.PK)))
			if err != nil {
				return nil, fmt.Errorf("SSH private key: %w", err)
			}
			config.Auth = append(config.Auth, ssh.PublicKeys(signer))
		}
		if c.Password != "" {
			config.Auth = append(config.Auth, ssh.Password(c.Password))
		}
		if len(config.Auth) == 0 {
			return nil, errors.New("SSH backend needs a password or a private key")
		}
		return &sshBackend{source: source, config: config}, nil
	case "shadowsocks":
		cipher, err := core.PickCipher(strings.ToUpper(c.Method), nil, c.Password)
		if err != nil {
			return nil, fmt.Errorf("Shadowsocks: %w", err)
		}
		return &shadowsocksBackend{source: source, cipher: cipher}, nil
	}
	return nil, fmt.Errorf("unsupported backend %q", c.Type)
}

// pemKey restores a key shared without its PEM armour (dnst:// pk).
func pemKey(key string) string {
	if strings.Contains(key, "-----BEGIN") {
		return key
	}
	body := strings.NewReplacer("-", "+", "_", "/").Replace(strings.TrimSpace(key))
	for len(body)%4 != 0 {
		body += "="
	}
	kind := "OPENSSH PRIVATE KEY"
	if raw, err := base64.StdEncoding.DecodeString(body); err == nil && !strings.HasPrefix(string(raw), "openssh-key-v1") {
		kind = "PRIVATE KEY"
	}
	var b strings.Builder
	b.WriteString("-----BEGIN " + kind + "-----\n")
	for len(body) > 64 {
		b.WriteString(body[:64] + "\n")
		body = body[64:]
	}
	b.WriteString(body + "\n-----END " + kind + "-----\n")
	return b.String()
}

// checkTarget is what a readiness check asks the backend to reach.
const checkTarget = "www.gstatic.com:80"

type streamDialer struct {
	ctx    context.Context
	source streamSource
}

func (d streamDialer) Dial(_, _ string) (net.Conn, error) { return d.source.Open(d.ctx) }

type socksBackend struct {
	source     streamSource
	user, pass string
}

func (b *socksBackend) Dial(ctx context.Context, target string) (net.Conn, error) {
	var auth *proxy.Auth
	if b.user != "" || b.pass != "" {
		auth = &proxy.Auth{User: b.user, Password: b.pass}
	}
	dialer, err := proxy.SOCKS5("tcp", "backend", auth, streamDialer{ctx, b.source})
	if err != nil {
		return nil, err
	}
	return dialer.(proxy.ContextDialer).DialContext(ctx, "tcp", target)
}

func (b *socksBackend) Check(ctx context.Context) error {
	conn, err := b.Dial(ctx, checkTarget)
	if err == nil {
		conn.Close()
	}
	return err
}

type shadowsocksBackend struct {
	source streamSource
	cipher core.Cipher
}

func (b *shadowsocksBackend) Dial(ctx context.Context, target string) (net.Conn, error) {
	address := socks.ParseAddr(target)
	if address == nil {
		return nil, fmt.Errorf("bad target %q", target)
	}
	stream, err := b.source.Open(ctx)
	if err != nil {
		return nil, err
	}
	conn := b.cipher.StreamConn(stream)
	if _, err := conn.Write(address); err != nil {
		stream.Close()
		return nil, err
	}
	return conn, nil
}

// Check opens a stream only: Shadowsocks gives no answer before data flows.
func (b *shadowsocksBackend) Check(ctx context.Context) error {
	stream, err := b.source.Open(ctx)
	if err == nil {
		stream.Close()
	}
	return err
}

type sshBackend struct {
	source streamSource
	config *ssh.ClientConfig
	mu     sync.Mutex
	client *ssh.Client
}

func (b *sshBackend) connect(ctx context.Context) (*ssh.Client, error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.client != nil {
		return b.client, nil
	}
	stream, err := b.source.Open(ctx)
	if err != nil {
		return nil, err
	}
	conn, channels, requests, err := ssh.NewClientConn(stream, "backend", b.config)
	if err != nil {
		stream.Close()
		return nil, fmt.Errorf("SSH: %w", err)
	}
	client := ssh.NewClient(conn, channels, requests)
	b.client = client
	go func() {
		_ = client.Wait()
		b.mu.Lock()
		if b.client == client {
			b.client = nil
		}
		b.mu.Unlock()
	}()
	return client, nil
}

func (b *sshBackend) Dial(ctx context.Context, target string) (net.Conn, error) {
	for attempt := 0; attempt < 2; attempt++ {
		client, err := b.connect(ctx)
		if err != nil {
			return nil, err
		}
		conn, err := client.DialContext(ctx, "tcp", target)
		if err == nil {
			return conn, nil
		}
		if ctx.Err() != nil {
			return nil, ctx.Err()
		}
		// A dead SSH session: drop it and log in again once.
		b.mu.Lock()
		if b.client == client {
			_ = client.Close()
			b.client = nil
		}
		b.mu.Unlock()
	}
	return nil, errors.New("SSH session could not be restored")
}

func (b *sshBackend) Check(ctx context.Context) error {
	_, err := b.connect(ctx)
	return err
}
