package main

import (
	"context"
	"crypto/subtle"
	"encoding/binary"
	"fmt"
	"io"
	"log"
	"net"
	"strconv"
	"sync"
	"time"
)

type bridgeDial func(context.Context, string, string) (net.Conn, error)

func readSOCKSAddress(r io.Reader) (string, error) {
	var t [1]byte
	if _, e := io.ReadFull(r, t[:]); e != nil {
		return "", e
	}
	var host string
	switch t[0] {
	case 1, 4:
		n := 4
		if t[0] == 4 {
			n = 16
		}
		b := make([]byte, n)
		if _, e := io.ReadFull(r, b); e != nil {
			return "", e
		}
		host = net.IP(b).String()
	case 3:
		if _, e := io.ReadFull(r, t[:]); e != nil {
			return "", e
		}
		if t[0] == 0 {
			return "", fmt.Errorf("empty SOCKS domain")
		}
		b := make([]byte, int(t[0]))
		if _, e := io.ReadFull(r, b); e != nil {
			return "", e
		}
		host = string(b)
	default:
		return "", fmt.Errorf("unsupported SOCKS address")
	}
	var port [2]byte
	if _, e := io.ReadFull(r, port[:]); e != nil {
		return "", e
	}
	return net.JoinHostPort(host, strconv.Itoa(int(binary.BigEndian.Uint16(port[:])))), nil
}

func encodeSOCKSAddress(address string) []byte {
	host, port, e := net.SplitHostPort(address)
	if e != nil {
		return nil
	}
	p, e := strconv.Atoi(port)
	if e != nil || p < 0 || p > 65535 {
		return nil
	}
	ip := net.ParseIP(host)
	var b []byte
	if v4 := ip.To4(); v4 != nil {
		b = append([]byte{1}, v4...)
	} else if ip != nil {
		b = append([]byte{4}, ip.To16()...)
	} else {
		if len(host) < 1 || len(host) > 255 {
			return nil
		}
		b = append([]byte{3, byte(len(host))}, host...)
	}
	return append(b, byte(p>>8), byte(p))
}

func socksReply(c net.Conn, code byte, address string) error {
	a := encodeSOCKSAddress(address)
	if a == nil {
		a = []byte{1, 0, 0, 0, 0, 0, 0}
	}
	_, e := c.Write(append([]byte{5, code, 0}, a...))
	return e
}

func socksHandshake(c net.Conn, user, pass string) error {
	_ = c.SetDeadline(time.Now().Add(15 * time.Second))
	var hdr [2]byte
	if _, e := io.ReadFull(c, hdr[:]); e != nil {
		return e
	}
	if hdr[0] != 5 || hdr[1] == 0 {
		return fmt.Errorf("invalid SOCKS greeting")
	}
	methods := make([]byte, int(hdr[1]))
	if _, e := io.ReadFull(c, methods); e != nil {
		return e
	}
	want := byte(0)
	if user != "" {
		want = 2
	}
	ok := false
	for _, m := range methods {
		if m == want {
			ok = true
		}
	}
	if !ok {
		_, _ = c.Write([]byte{5, 255})
		return fmt.Errorf("SOCKS authentication method required")
	}
	if _, e := c.Write([]byte{5, want}); e != nil {
		return e
	}
	if want == 2 {
		if _, e := io.ReadFull(c, hdr[:]); e != nil {
			return e
		}
		if hdr[0] != 1 || hdr[1] == 0 {
			return fmt.Errorf("invalid SOCKS authentication")
		}
		u := make([]byte, int(hdr[1]))
		if _, e := io.ReadFull(c, u); e != nil {
			return e
		}
		var length [1]byte
		if _, e := io.ReadFull(c, length[:]); e != nil {
			return e
		}
		p := make([]byte, int(length[0]))
		if _, e := io.ReadFull(c, p); e != nil {
			return e
		}
		valid := subtle.ConstantTimeCompare(u, []byte(user)) & subtle.ConstantTimeCompare(p, []byte(pass))
		status := byte(1)
		if valid == 1 {
			status = 0
		}
		if _, e := c.Write([]byte{1, status}); e != nil {
			return e
		}
		if status != 0 {
			return fmt.Errorf("SOCKS authentication failed")
		}
	}
	return nil
}

func serveBridgeSOCKS(ctx context.Context, listener net.Listener, dial bridgeDial, user, pass string) error {
	var wg sync.WaitGroup
	stop := context.AfterFunc(ctx, func() { _ = listener.Close() })
	defer stop()
	defer wg.Wait()
	for {
		c, e := listener.Accept()
		if e != nil {
			if ctx.Err() != nil {
				return nil
			}
			return e
		}
		wg.Add(1)
		go func() {
			defer wg.Done()
			defer c.Close()
			closeOnCancel := context.AfterFunc(ctx, func() { _ = c.Close() })
			defer closeOnCancel()
			if socksHandshake(c, user, pass) != nil {
				return
			}
			var h [3]byte
			if _, e := io.ReadFull(c, h[:]); e != nil || h[0] != 5 || h[2] != 0 {
				return
			}
			address, e := readSOCKSAddress(c)
			if e != nil {
				_ = socksReply(c, 8, "")
				return
			}
			_ = c.SetDeadline(time.Time{})
			switch h[1] {
			case 1:
				dialCtx, cancel := context.WithTimeout(ctx, 20*time.Second)
				upstream, e := dial(dialCtx, "tcp", address)
				cancel()
				if e != nil {
					_ = socksReply(c, 5, "")
					return
				}
				defer upstream.Close()
				stopUpstream := context.AfterFunc(ctx, func() { _ = upstream.Close() })
				defer stopUpstream()
				if socksReply(c, 0, upstream.LocalAddr().String()) != nil {
					return
				}
				finished := make(chan struct{}, 1)
				go func() {
					_, err := io.Copy(upstream, c)
					if half, ok := upstream.(interface{ CloseWrite() error }); ok && err == nil {
						_ = half.CloseWrite()
					} else {
						_ = upstream.Close()
					}
					finished <- struct{}{}
				}()
				_, _ = io.Copy(c, upstream)
				_ = c.Close()
				_ = upstream.Close()
				<-finished
			case 3:
				_ = socksUDPAssociation(ctx, c, dial)
			default:
				_ = socksReply(c, 7, "")
			}
		}()
	}
}

// Each association belongs to its authenticated TCP control connection. All
// destination sockets are created by the tunnel's netstack, never by the host.
func socksUDPAssociation(parent context.Context, control net.Conn, dial bridgeDial) error {
	ctx, cancel := context.WithCancel(parent)
	defer cancel()
	udp, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		_ = socksReply(control, 1, "")
		return err
	}
	defer udp.Close()
	stop := context.AfterFunc(ctx, func() { _ = udp.Close() })
	defer stop()
	if err = socksReply(control, 0, udp.LocalAddr().String()); err != nil {
		return err
	}
	controlDone := make(chan struct{})
	go func() { _, _ = io.Copy(io.Discard, control); cancel(); close(controlDone) }()
	defer func() { _ = control.Close(); <-controlDone }()
	var mu sync.Mutex
	flows := make(map[string]net.Conn)
	var wg sync.WaitGroup
	defer func() {
		cancel()
		mu.Lock()
		for _, conn := range flows {
			_ = conn.Close()
		}
		mu.Unlock()
		wg.Wait()
	}()
	var client *net.UDPAddr
	controlIP := control.RemoteAddr().(*net.TCPAddr).IP
	buf := make([]byte, 65535)
	for {
		n, source, e := udp.ReadFromUDP(buf)
		if e != nil {
			if ctx.Err() != nil {
				return nil
			}
			return e
		}
		if !source.IP.Equal(controlIP) || n < 4 || buf[0] != 0 || buf[1] != 0 || buf[2] != 0 {
			continue
		}
		if client == nil {
			client = source
		}
		if source.String() != client.String() {
			continue
		}
		r := &byteReader{b: buf[3:n]}
		destination, e := readSOCKSAddress(r)
		if e != nil {
			continue
		}
		payload := r.b
		mu.Lock()
		conn := flows[destination]
		if conn == nil && len(flows) < 256 {
			dialCtx, stopDial := context.WithTimeout(ctx, 10*time.Second)
			conn, e = dial(dialCtx, "udp", destination)
			stopDial()
			if e == nil {
				flows[destination] = conn
				wg.Add(1)
				go func(conn net.Conn, target string, replyTo *net.UDPAddr) {
					defer wg.Done()
					defer conn.Close()
					defer func() {
						mu.Lock()
						if flows[target] == conn {
							delete(flows, target)
						}
						mu.Unlock()
					}()
					response := make([]byte, 65535)
					for {
						_ = conn.SetReadDeadline(time.Now().Add(2 * time.Minute))
						n, e := conn.Read(response)
						if e != nil {
							return
						}
						header := append([]byte{0, 0, 0}, encodeSOCKSAddress(target)...)
						if _, e = udp.WriteToUDP(append(header, response[:n]...), replyTo); e != nil {
							return
						}
					}
				}(conn, destination, client)
			}
		}
		mu.Unlock()
		if conn != nil {
			_ = conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
			_, _ = conn.Write(payload)
		}
	}
}

type byteReader struct{ b []byte }

func (r *byteReader) Read(p []byte) (int, error) {
	if len(r.b) == 0 {
		return 0, io.EOF
	}
	n := copy(p, r.b)
	r.b = r.b[n:]
	return n, nil
}

func runBridgeSOCKS(ctx context.Context, address string, dial bridgeDial, user, pass string) error {
	listener, err := net.Listen("tcp4", address)
	if err != nil {
		return err
	}
	defer listener.Close()
	log.Printf("[qWDTT] SOCKS bridge ready: %s (TCP/UDP)", address)
	return serveBridgeSOCKS(ctx, listener, dial, user, pass)
}
