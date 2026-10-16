package main

import (
	"context"
	"errors"
	"net"
	"testing"
	"time"
)

func TestPingCancellationInterruptsSilentTURNAllocation(t *testing.T) {
	listener, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	go func() {
		conn, err := listener.Accept()
		if err != nil {
			return
		}
		defer conn.Close()
		b := make([]byte, 4096)
		for {
			if _, err = conn.Read(b); err != nil {
				return
			}
		}
	}()
	ctx, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
	defer cancel()
	start := time.Now()
	_, err = RunPing(ctx, &TurnParams{TCPTransport: true}, &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 56000}, &Credentials{User: "test", Pass: "test", TurnURLs: []string{listener.Addr().String()}})
	if err == nil || time.Since(start) > time.Second {
		t.Fatalf("silent relay ignored cancellation: %v", err)
	}
}

func TestPingAuthorizationDoesNotConsumeServerTimeout(t *testing.T) {
	delay, err := runPingProbe(context.Background(), []string{"hash"}, time.Second, 20*time.Millisecond,
		func(ctx context.Context, _ string, _ int) (string, string, []string, error) {
			select {
			case <-time.After(40 * time.Millisecond):
				return "u", "p", []string{"relay:1"}, nil
			case <-ctx.Done():
				return "", "", nil, ctx.Err()
			}
		}, func(ctx context.Context, _ *Credentials) (int64, error) {
			if ctx.Err() != nil {
				t.Fatal("authentication consumed handshake budget")
			}
			deadline, ok := ctx.Deadline()
			if !ok || time.Until(deadline) > 30*time.Millisecond {
				t.Fatal("server timeout not applied")
			}
			return 57, nil
		})
	if err != nil || delay != 57 {
		t.Fatalf("ping: %d %v", delay, err)
	}
}

func TestPingServerAndAuthTimeoutsCancel(t *testing.T) {
	for _, auth := range []bool{true, false} {
		start := time.Now()
		_, err := runPingProbe(context.Background(), []string{"hash"}, 30*time.Millisecond, 30*time.Millisecond,
			func(ctx context.Context, _ string, _ int) (string, string, []string, error) {
				if auth {
					<-ctx.Done()
					return "", "", nil, ctx.Err()
				}
				return "u", "p", nil, nil
			},
			func(ctx context.Context, _ *Credentials) (int64, error) { <-ctx.Done(); return 0, ctx.Err() })
		if !errors.Is(err, context.DeadlineExceeded) || time.Since(start) > time.Second {
			t.Fatalf("auth=%v: %v", auth, err)
		}
	}
}
