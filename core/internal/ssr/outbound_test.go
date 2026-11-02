package ssr

import (
	"context"
	"fmt"
	"io"
	"net"
	"testing"
	"time"

	"github.com/metacubex/mihomo/transport/shadowsocks/core"
	box "github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/include"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing/common/json"
	M "github.com/sagernet/sing/common/metadata"
)

func testOutbound(t *testing.T, port int, method string) adapter.Outbound {
	t.Helper()
	registry := include.OutboundRegistry()
	Register(registry)
	ctx := box.Context(context.Background(), include.InboundRegistry(), registry, include.EndpointRegistry(),
		include.DNSTransportRegistry(), include.ServiceRegistry(), include.CertificateProviderRegistry())
	text := fmt.Sprintf(`{"outbounds":[{"type":"direct","tag":"direct","connect_timeout":"3s"},{"type":"shadowsocksr","tag":"ssr","server":"127.0.0.1","server_port":%d,"method":%q,"password":"test","protocol":"origin","obfs":"plain","detour":"direct"}],"route":{"final":"ssr"}}`, port, method)
	options, err := json.UnmarshalExtendedContext[option.Options](ctx, []byte(text))
	if err != nil {
		t.Fatal(err)
	}
	instance, err := box.New(box.Options{Context: ctx, Options: options})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { instance.Close() })
	if err = instance.Start(); err != nil {
		t.Fatal(err)
	}
	out, ok := instance.Outbound().Outbound("ssr")
	if !ok {
		t.Fatal("SSR outbound not registered")
	}
	return out
}

func TestTCPThroughDetour(t *testing.T) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	result := make(chan error, 1)
	go func() {
		conn, err := listener.Accept()
		if err != nil {
			result <- err
			return
		}
		defer conn.Close()
		conn.SetDeadline(time.Now().Add(3 * time.Second))
		cipher, err := core.PickCipher("aes-256-cfb", nil, "test")
		if err != nil {
			result <- err
			return
		}
		stream := cipher.StreamConn(conn)
		destination, err := M.SocksaddrSerializer.ReadAddrPort(stream)
		if err != nil || destination.String() != "example.test:443" {
			result <- fmt.Errorf("wrong SSR destination: %s: %v", destination, err)
			return
		}
		_, err = io.Copy(stream, stream)
		result <- err
	}()
	out := testOutbound(t, listener.Addr().(*net.TCPAddr).Port, "aes-256-cfb")
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	conn, err := out.DialContext(ctx, "tcp", M.ParseSocksaddr("example.test:443"))
	if err != nil {
		t.Fatal(err)
	}
	conn.SetDeadline(time.Now().Add(3 * time.Second))
	if _, err = conn.Write([]byte("ping")); err != nil {
		t.Fatal(err)
	}
	var response [4]byte
	if _, err = io.ReadFull(conn, response[:]); err != nil || string(response[:]) != "ping" {
		t.Fatalf("TCP response %q: %v", response, err)
	}
	conn.Close()
	if err = <-result; err != nil {
		t.Fatal(err)
	}
}

func TestUDPAddressAndLength(t *testing.T) {
	for _, malformed := range []bool{false, true} {
		t.Run(fmt.Sprint(malformed), func(t *testing.T) {
			server, err := net.ListenPacket("udp", "127.0.0.1:0")
			if err != nil {
				t.Fatal(err)
			}
			defer server.Close()
			go func() {
				server.SetDeadline(time.Now().Add(3 * time.Second))
				data := make([]byte, 2048)
				n, addr, err := server.ReadFrom(data)
				if err == nil {
					if malformed {
						server.WriteTo([]byte{1}, addr)
					} else {
						server.WriteTo(data[:n], addr)
					}
				}
			}()
			out := testOutbound(t, server.LocalAddr().(*net.UDPAddr).Port, "none")
			conn, err := out.ListenPacket(context.Background(), M.ParseSocksaddr("192.0.2.5:53"))
			if err != nil {
				t.Fatal(err)
			}
			defer conn.Close()
			conn.SetDeadline(time.Now().Add(3 * time.Second))
			n, err := conn.WriteTo([]byte("dns"), &net.UDPAddr{IP: net.ParseIP("192.0.2.5"), Port: 53})
			if err != nil || n != 3 {
				t.Fatalf("UDP WriteTo=%d, %v", n, err)
			}
			var data [2048]byte
			n, addr, err := conn.ReadFrom(data[:])
			if malformed {
				if err == nil {
					t.Fatal("malformed SSR datagram accepted")
				}
				return
			}
			if err != nil || string(data[:n]) != "dns" || addr.String() != "192.0.2.5:53" {
				t.Fatalf("UDP response %q, %v, %v", data[:n], addr, err)
			}
		})
	}
}
