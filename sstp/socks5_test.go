package main

import (
	"io"
	"net"
	"testing"
	"time"
)

func TestUDPHeaders(t *testing.T) {
	for _, ip := range []net.IP{net.ParseIP("192.0.2.7"), net.ParseIP("2001:db8::7")} {
		packet := buildSOCKSUDPHeader(ip, 53, []byte("dns"))
		host, port, offset, err := parseSOCKSUDPHeader(packet)
		if err != nil || !net.ParseIP(host).Equal(ip) || port != 53 || string(packet[offset:]) != "dns" {
			t.Fatalf("UDP round trip: %s %d %d %v", host, port, offset, err)
		}
		for _, field := range []int{0, 1, 2} {
			bad := append([]byte(nil), packet...)
			bad[field] = 1
			if _, _, _, err := parseSOCKSUDPHeader(bad); err == nil {
				t.Fatalf("accepted invalid reserved/fragment byte %d", field)
			}
		}
		for i := 0; i < offset; i++ {
			if _, _, _, err := parseSOCKSUDPHeader(packet[:i]); err == nil {
				t.Fatal("accepted truncated header")
			}
		}
	}
}

func TestSOCKSRequiresAuthenticationAndReservedUDPPort(t *testing.T) {
	proxy := &SOCKSProxy{socksUser: "user", socksPass: "pass"}
	for _, authenticated := range []bool{false, true} {
		server, client := net.Pipe()
		done := make(chan struct{})
		go func() { defer close(done); proxy.handleSOCKS(server) }()
		client.SetDeadline(time.Now().Add(time.Second))
		method := byte(0)
		if authenticated {
			method = 2
		}
		client.Write([]byte{5, 1, method})
		response := make([]byte, 2)
		if _, err := io.ReadFull(client, response); err != nil {
			t.Fatal(err)
		}
		if !authenticated {
			if response[1] != 255 {
				t.Fatal("accepted unauthenticated client")
			}
		} else {
			client.Write([]byte{1, 4, 'u', 's', 'e', 'r', 4, 'p', 'a', 's', 's'})
			if _, err := io.ReadFull(client, response); err != nil || response[1] != 0 {
				t.Fatal("authentication failed", err)
			}
			client.Write([]byte{5, 3, 0, 1, 127, 0, 0, 1, 0, 0})
			if _, err := io.ReadFull(client, response); err != nil || response[1] != 2 {
				t.Fatal("accepted unknown UDP source port", err)
			}
		}
		client.Close()
		select {
		case <-done:
		case <-time.After(time.Second):
			t.Fatal("SOCKS handler did not finish")
		}
	}
}
