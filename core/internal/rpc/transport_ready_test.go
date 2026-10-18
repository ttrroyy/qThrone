package rpc

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"testing"
	"time"
)

// fakeSOCKS answers the SOCKS5 negotiation: method 2 checks user/pass, method 0 lets anyone in.
func fakeSOCKS(t *testing.T, method byte, user, pass string) net.Listener {
	t.Helper()
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	go func() {
		for {
			conn, err := ln.Accept()
			if err != nil {
				return
			}
			go func(conn net.Conn) {
				defer conn.Close()
				head := make([]byte, 3)
				if _, err := io.ReadFull(conn, head); err != nil {
					return
				}
				conn.Write([]byte{5, method})
				if method != 2 {
					return
				}
				b := make([]byte, 2)
				if _, err := io.ReadFull(conn, b); err != nil {
					return
				}
				u := make([]byte, b[1])
				io.ReadFull(conn, u)
				l := make([]byte, 1)
				io.ReadFull(conn, l)
				p := make([]byte, l[0])
				io.ReadFull(conn, p)
				if string(u) == user && string(p) == pass {
					conn.Write([]byte{1, 0})
				} else {
					conn.Write([]byte{1, 1})
				}
			}(conn)
		}
	}()
	return ln
}

func readyConfig(address, user, pass string) string {
	return fmt.Sprintf(`{"socks":%q,"socks_user":%q,"socks_pass":%q}`, address, user, pass)
}

func TestTransportReadinessNeedsAuthenticatedSOCKS(t *testing.T) {
	ln := fakeSOCKS(t, 2, "launch-user", "launch-pass")
	config := readyConfig(ln.Addr().String(), "launch-user", "launch-pass")
	if err := waitTransportReady(context.Background(), config, nil); err != nil {
		t.Fatal(err)
	}
	if err := waitTransportReady(context.Background(), readyConfig(ln.Addr().String(), "launch-user", "other"), nil); !errors.Is(err, errSOCKSRejected) {
		t.Fatalf("wrong credentials reported %v", err)
	}
	if err := waitTransportReady(context.Background(), readyConfig(ln.Addr().String(), "", ""), nil); err == nil {
		t.Fatal("a launch without credentials reported ready")
	}
	ln.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()
	if err := waitTransportReady(ctx, config, nil); err == nil {
		t.Fatal("reported a tunnel ready before its listener existed")
	}
	done := make(chan struct{})
	close(done)
	if err := waitTransportReady(context.Background(), config, done); err == nil {
		t.Fatal("exited helper reported ready")
	}
}

func TestTransportReadinessRefusesOpenSOCKS(t *testing.T) {
	ln := fakeSOCKS(t, 0, "", "")
	defer ln.Close()
	if err := waitTransportReady(context.Background(), readyConfig(ln.Addr().String(), "u", "p"), nil); !errors.Is(err, errSOCKSUnauthenticated) {
		t.Fatalf("a listener without authentication reported %v", err)
	}
}
