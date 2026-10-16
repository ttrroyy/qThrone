package rpc

import (
	"context"
	"fmt"
	"net"
	"testing"
	"time"
)

func TestTransportReadinessWaitsForListener(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	config := fmt.Sprintf(`{"socks":%q}`, ln.Addr().String())
	if err := waitTransportReady(context.Background(), config, nil); err != nil {
		t.Fatal(err)
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
