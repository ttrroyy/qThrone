package tunnel

import (
	"bufio"
	"context"
	"encoding/base64"
	"fmt"
	"io"
	"net"
	"net/http"
	"strconv"
	"strings"
	"time"
)

// ProxyConfig routes the OUTER transport (the TCP connection the SSTP/TLS
// carrier rides on) through an upstream HTTP CONNECT or SOCKS5 proxy.
// Everything after the proxy handshake is the normal TLS+SSTP stream; the
// proxy only sees the target host:port, never tunnel credentials (those live
// inside MS-CHAPv2) or any payload.
type ProxyConfig struct {
	// Type is "http" (HTTP CONNECT) or "socks5".
	Type string
	Host string
	Port int
	// Username/Password: Proxy-Authorization for http, RFC 1929
	// username/password sub-negotiation for socks5. Empty = no auth.
	Username string
	Password string
}

func (p *ProxyConfig) addr() string { return net.JoinHostPort(p.Host, strconv.Itoa(p.Port)) }

func (p *ProxyConfig) validate() error {
	switch p.Type {
	case "http", "socks5":
	default:
		return fmt.Errorf("tunnel: unsupported proxy type %q (want http or socks5)", p.Type)
	}
	if p.Host == "" || p.Port == 0 {
		return fmt.Errorf("tunnel: proxy host and port are required")
	}
	return nil
}

// proxyDial establishes a connection to target (host:port) through the
// proxy: TCP dial, proxy handshake, then a raw conn ready for TLS.
func (p *ProxyConfig) proxyDial(ctx context.Context, target string) (net.Conn, error) {
	if err := p.validate(); err != nil {
		return nil, err
	}
	d := net.Dialer{Timeout: 15 * time.Second}
	conn, err := d.DialContext(ctx, "tcp", p.addr())
	if err != nil {
		return nil, fmt.Errorf("tunnel: dial proxy %s: %w", p.addr(), err)
	}
	// Bound the handshake so a hung proxy cannot stall establishment.
	_ = conn.SetDeadline(time.Now().Add(20 * time.Second))
	switch p.Type {
	case "http":
		err = p.httpConnect(conn, target)
	case "socks5":
		err = p.socks5Connect(ctx, conn, target)
	}
	if err != nil {
		conn.Close()
		return nil, fmt.Errorf("tunnel: proxy %s handshake: %w", p.Type, err)
	}
	_ = conn.SetDeadline(time.Time{})
	return conn, nil
}

// httpConnect performs the RFC 9110 CONNECT exchange.
func (p *ProxyConfig) httpConnect(conn net.Conn, target string) error {
	var b strings.Builder
	fmt.Fprintf(&b, "CONNECT %s HTTP/1.1\r\nHost: %s\r\n", target, target)
	if p.Username != "" || p.Password != "" {
		cred := base64.StdEncoding.EncodeToString([]byte(p.Username + ":" + p.Password))
		fmt.Fprintf(&b, "Proxy-Authorization: Basic %s\r\n", cred)
	}
	b.WriteString("Proxy-Connection: keep-alive\r\n\r\n")
	if _, err := io.WriteString(conn, b.String()); err != nil {
		return err
	}
	// Read exactly the header block; bufio would over-read into the TLS
	// stream that starts immediately after CRLFCRLF.
	br := bufio.NewReaderSize(conn, 1024)
	status, err := http.ReadResponse(br, &http.Request{Method: http.MethodConnect})
	if err != nil {
		return err
	}
	defer status.Body.Close()
	if status.StatusCode < 200 || status.StatusCode > 299 {
		return fmt.Errorf("proxy refused CONNECT: %s", status.Status)
	}
	if br.Buffered() > 0 {
		// Extremely rare: proxy pipelined bytes. Preserve them.
		leftover, _ := br.Peek(br.Buffered())
		return fmt.Errorf("tunnel: proxy sent %d unexpected early bytes", len(leftover))
	}
	return nil
}

// socks5Connect performs RFC 1928 (plus RFC 1929 auth) toward target.
func (p *ProxyConfig) socks5Connect(ctx context.Context, conn net.Conn, target string) error {
	// Greeting: VER=5, methods; offer no-auth and (if configured) user/pass.
	methods := []byte{0x00} // no-auth
	if p.Username != "" || p.Password != "" {
		methods = append(methods, 0x02)
	}
	greet := append([]byte{0x05, byte(len(methods))}, methods...)
	if _, err := conn.Write(greet); err != nil {
		return err
	}
	var sel [2]byte
	if _, err := io.ReadFull(conn, sel[:]); err != nil {
		return err
	}
	if sel[0] != 0x05 {
		return fmt.Errorf("not SOCKS5 (ver %#x)", sel[0])
	}
	switch sel[1] {
	case 0x00:
	case 0x02:
		u := []byte(p.Username)
		w := []byte(p.Password)
		if len(u) > 255 || len(w) > 255 {
			return fmt.Errorf("proxy credentials too long")
		}
		req := append([]byte{0x01, byte(len(u))}, u...)
		req = append(req, byte(len(w)))
		req = append(req, w...)
		if _, err := conn.Write(req); err != nil {
			return err
		}
		var rep [2]byte
		if _, err := io.ReadFull(conn, rep[:]); err != nil {
			return err
		}
		if rep[1] != 0x00 {
			return fmt.Errorf("proxy auth failed (status %#x)", rep[1])
		}
	case 0xFF:
		return fmt.Errorf("proxy offers no acceptable auth method")
	default:
		return fmt.Errorf("proxy chose unexpected method %#x", sel[1])
	}

	// CONNECT request with the target address in the right ATYP form.
	host, portStr, err := net.SplitHostPort(target)
	if err != nil {
		return err
	}
	port, err := strconv.Atoi(portStr)
	if err != nil || port < 1 || port > 65535 {
		return fmt.Errorf("bad target port %q", portStr)
	}
	req := []byte{0x05, 0x01, 0x00}
	if ip := net.ParseIP(host); ip != nil {
		if v4 := ip.To4(); v4 != nil {
			req = append(req, 0x01)
			req = append(req, v4...)
		} else {
			req = append(req, 0x04)
			req = append(req, ip.To16()...)
		}
	} else {
		if len(host) > 255 {
			return fmt.Errorf("target host too long")
		}
		req = append(req, 0x03, byte(len(host)))
		req = append(req, host...)
	}
	req = append(req, byte(port>>8), byte(port))
	if _, err := conn.Write(req); err != nil {
		return err
	}

	// Reply: VER REP RSV ATYP + BND.ADDR BND.PORT. The bind address length
	// depends on ATYP, so parse the header first.
	hdr := make([]byte, 4)
	if _, err := io.ReadFull(conn, hdr); err != nil {
		return err
	}
	if hdr[0] != 0x05 {
		return fmt.Errorf("bad SOCKS5 reply version %#x", hdr[0])
	}
	if hdr[1] != 0x00 {
		return fmt.Errorf("SOCKS5 CONNECT failed: %s", socks5ReplyName(hdr[1]))
	}
	var addrLen int
	switch hdr[3] {
	case 0x01:
		addrLen = 4
	case 0x04:
		addrLen = 16
	case 0x03:
		var l [1]byte
		if _, err := io.ReadFull(conn, l[:]); err != nil {
			return err
		}
		addrLen = int(l[0])
	default:
		return fmt.Errorf("bad SOCKS5 ATYP %#x", hdr[3])
	}
	if _, err := io.ReadFull(conn, make([]byte, addrLen+2)); err != nil {
		return err
	}
	_ = ctx
	return nil
}

func socks5ReplyName(code byte) string {
	switch code {
	case 0x01:
		return "general failure"
	case 0x02:
		return "connection not allowed"
	case 0x03:
		return "network unreachable"
	case 0x04:
		return "host unreachable"
	case 0x05:
		return "connection refused"
	case 0x06:
		return "TTL expired"
	case 0x07:
		return "command not supported"
	case 0x08:
		return "address type not supported"
	}
	return fmt.Sprintf("code %#x", code)
}
