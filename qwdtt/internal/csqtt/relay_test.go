package csqtt

import (
	"context"
	"errors"
	"net"
	"testing"
	"time"
)

func TestRelayPacketsAndCancellation(t *testing.T) {
	server, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()
	socket, err := net.DialUDP("udp4", nil, server.LocalAddr().(*net.UDPAddr))
	if err != nil {
		t.Fatal(err)
	}
	device, application := net.Pipe()
	defer application.Close()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() { done <- RelayIP(ctx, device, socket) }()
	application.SetDeadline(time.Now().Add(time.Second))
	server.SetDeadline(time.Now().Add(time.Second))
	packet := []byte{0x45, 0, 0, 4}
	if _, err := application.Write(packet); err != nil {
		t.Fatal(err)
	}
	buf := make([]byte, 100)
	n, address, err := server.ReadFromUDP(buf)
	if err != nil || string(buf[:n]) != string(packet) {
		t.Fatal("outbound packet changed")
	}
	if _, err := server.WriteToUDP(packet, address); err != nil {
		t.Fatal(err)
	}
	n, err = application.Read(buf)
	if err != nil || string(buf[:n]) != string(packet) {
		t.Fatal("inbound packet changed")
	}
	cancel()
	select {
	case err := <-done:
		if !errors.Is(err, context.Canceled) {
			t.Fatalf("cancel: %v", err)
		}
	case <-time.After(time.Second):
		t.Fatal("relay did not stop")
	}
}
