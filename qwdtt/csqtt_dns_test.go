package main

import (
	"context"
	"encoding/binary"
	"golang.org/x/net/dns/dnsmessage"
	"io"
	"net"
	"testing"
	"time"
)

func TestCSQTTDNSUsesTunnelTCP(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	dial := func(ctx context.Context, network, address string) (net.Conn, error) {
		if network != "tcp" {
			t.Errorf("unexpected DNS network %s", network)
		}
		a, b := net.Pipe()
		if address == "203.0.113.53:53" {
			go func() {
				defer b.Close()
				var length [2]byte
				if _, err := io.ReadFull(b, length[:]); err != nil {
					return
				}
				raw := make([]byte, binary.BigEndian.Uint16(length[:]))
				if _, err := io.ReadFull(b, raw); err != nil {
					return
				}
				var query dnsmessage.Message
				if query.Unpack(raw) != nil {
					return
				}
				response := dnsmessage.Message{Header: dnsmessage.Header{ID: query.ID, Response: true, RecursionAvailable: true}, Questions: query.Questions,
					Answers: []dnsmessage.Resource{{Header: dnsmessage.ResourceHeader{Name: query.Questions[0].Name, Type: dnsmessage.TypeA, Class: dnsmessage.ClassINET, TTL: 60}, Body: &dnsmessage.AResource{A: [4]byte{203, 0, 113, 7}}}}}
				packed, err := response.Pack()
				if err != nil {
					return
				}
				binary.BigEndian.PutUint16(length[:], uint16(len(packed)))
				b.Write(append(length[:], packed...))
			}()
		} else {
			if address != "203.0.113.7:80" {
				t.Errorf("unexpected tunnel destination %s", address)
			}
			b.Close()
		}
		return a, nil
	}
	conn, err := csqttTunnelDial(dial, []string{"203.0.113.53"})(ctx, "tcp", "example.invalid:80")
	if err != nil {
		t.Fatal(err)
	}
	conn.Close()
}
