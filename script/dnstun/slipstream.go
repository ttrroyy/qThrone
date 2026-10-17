package main

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"
)

// slipstream runs slipstream-client (Rust) as a child. On Android its build
// listens on a Unix socket inside the private session directory; the desktop
// build listens on a loopback port of its own, which this helper alone dials:
// qThrone only ever reaches the tunnel through the authenticated listener.
type slipstream struct {
	cmd     *exec.Cmd
	network string
	address string
	done    chan struct{}
}

func slipstreamExecutable(exe string) string {
	if runtime.GOOS == "android" {
		return filepath.Join(filepath.Dir(exe), "libslipstream.so")
	}
	name := "slipstream-client"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	return filepath.Join(filepath.Dir(exe), name)
}

func freeLoopbackPort() (int, error) {
	l, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		return 0, err
	}
	defer l.Close()
	return l.Addr().(*net.TCPAddr).Port, nil
}

func startSlipstream(ctx context.Context, c config, configPath string) (*slipstream, error) {
	exe, err := os.Executable()
	if err != nil {
		return nil, err
	}
	folder, err := filepath.Abs(filepath.Dir(configPath))
	if err != nil {
		return nil, err
	}
	s := &slipstream{done: make(chan struct{})}
	var args []string
	if runtime.GOOS == "android" {
		// Socket paths are limited to 108 bytes; the session directory alone comes
		// close on Android, so both processes use the name relative to it.
		if err := os.Chdir(folder); err != nil {
			return nil, err
		}
		s.network, s.address = "unix", "slipstream.sock"
		args = []string{"--tcp-listen-host", s.address, "--tcp-listen-port", "1"}
	} else {
		port, err := freeLoopbackPort()
		if err != nil {
			return nil, errors.New("no loopback port for the Slipstream client")
		}
		s.network, s.address = "tcp", net.JoinHostPort("127.0.0.1", strconv.Itoa(port))
		args = []string{"--tcp-listen-host", "127.0.0.1", "--tcp-listen-port", strconv.Itoa(port)}
	}
	args = append(args, "--domain", c.Domain)
	for _, r := range c.resolverList() {
		r = strings.TrimPrefix(strings.TrimSpace(r), "udp://")
		if strings.Contains(r, "://") {
			return nil, fmt.Errorf("Slipstream resolvers are UDP addresses, not %q", r)
		}
		flag := "--resolver"
		if c.Authoritative {
			flag = "--authoritative"
		}
		args = append(args, flag, withPort(r, "53"))
	}
	if cert := strings.TrimSpace(c.Cert); cert != "" {
		path := filepath.Join(folder, "slipstream.pem")
		if err := os.WriteFile(path, []byte(pemCert(cert)), 0o600); err != nil {
			return nil, err
		}
		args = append(args, "--cert", path)
	}
	switch c.Congestion {
	case "bbr", "dcubic":
		args = append(args, "--congestion-control", c.Congestion)
	}
	if c.KeepAliveMillis > 0 && c.KeepAliveMillis <= 65535 {
		args = append(args, "--keep-alive-interval", strconv.Itoa(c.KeepAliveMillis))
	}
	cmd := exec.CommandContext(ctx, slipstreamExecutable(exe), args...)
	cmd.Dir = folder
	hideChildWindow(cmd)
	output, err := cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}
	cmd.Stderr = cmd.Stdout
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("cannot start the Slipstream client: %w", err)
	}
	s.cmd = cmd
	go func() {
		scanner := bufio.NewScanner(io.LimitReader(output, 1<<30))
		for scanner.Scan() {
			fmt.Println("[Slipstream] " + scanner.Text())
		}
	}()
	go func() { _ = cmd.Wait(); close(s.done) }()
	deadline := time.Now().Add(10 * time.Second)
	for {
		if s.listening() {
			return s, nil
		}
		select {
		case <-s.done:
			return nil, errors.New("the Slipstream client stopped before listening")
		case <-time.After(50 * time.Millisecond):
		}
		if time.Now().After(deadline) {
			s.Close()
			return nil, errors.New("the Slipstream client did not start")
		}
	}
}

func (s *slipstream) listening() bool {
	if s.network == "unix" {
		_, err := os.Stat(s.address)
		return err == nil
	}
	conn, err := net.DialTimeout("tcp", s.address, 100*time.Millisecond)
	if err == nil {
		conn.Close()
	}
	return err == nil
}

func (s *slipstream) Open(ctx context.Context) (net.Conn, error) {
	select {
	case <-s.done:
		return nil, errors.New("the Slipstream client stopped")
	default:
	}
	var d net.Dialer
	return d.DialContext(ctx, s.network, s.address)
}

func (s *slipstream) Close() error {
	if s.cmd.Process != nil {
		// Windows has no interrupt for a child process; the client keeps no state to save.
		if runtime.GOOS == "windows" || s.cmd.Process.Signal(os.Interrupt) != nil {
			_ = s.cmd.Process.Kill()
		}
		select {
		case <-s.done:
		case <-time.After(2 * time.Second):
			_ = s.cmd.Process.Kill()
			<-s.done
		}
	}
	if s.network == "unix" {
		return os.Remove(s.address)
	}
	return nil
}

// pemCert restores a certificate shared as base64url DER (dnst:// cert).
func pemCert(cert string) string {
	if strings.Contains(cert, "-----BEGIN") {
		return cert
	}
	body := strings.NewReplacer("-", "+", "_", "/").Replace(strings.TrimSpace(cert))
	for len(body)%4 != 0 {
		body += "="
	}
	var b strings.Builder
	b.WriteString("-----BEGIN CERTIFICATE-----\n")
	for len(body) > 64 {
		b.WriteString(body[:64] + "\n")
		body = body[64:]
	}
	b.WriteString(body + "\n-----END CERTIFICATE-----\n")
	return b.String()
}
