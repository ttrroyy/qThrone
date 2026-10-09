package main

import (
	"bytes"
	"context"
	"crypto/ecdh"
	"crypto/rand"
	"encoding/base64"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"golang.zx2c4.com/wireguard/tun/netstack"
)

func testBridge(t *testing.T, dial bridgeDial) string {
	t.Helper()
	ctx, cancel := context.WithCancel(context.Background())
	ln, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	done := make(chan error, 1)
	go func() { done <- serveBridgeSOCKS(ctx, ln, dial, "user", "secret") }()
	t.Cleanup(func() {
		cancel()
		select {
		case e := <-done:
			if e != nil {
				t.Error(e)
			}
		case <-time.After(5 * time.Second):
			t.Error("bridge did not shut down")
		}
	})
	return ln.Addr().String()
}

func testControl(t *testing.T, address, password string) net.Conn {
	t.Helper()
	c, err := net.Dial("tcp", address)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = c.Close() })
	_ = c.SetDeadline(time.Now().Add(5 * time.Second))
	_, _ = c.Write([]byte{5, 1, 2})
	var answer [2]byte
	if _, err = io.ReadFull(c, answer[:]); err != nil || answer != [2]byte{5, 2} {
		t.Fatalf("greeting: %v %v", answer, err)
	}
	b := append([]byte{1, 4}, []byte("user")...)
	b = append(b, byte(len(password)))
	b = append(b, password...)
	_, _ = c.Write(b)
	if _, err = io.ReadFull(c, answer[:]); err != nil {
		t.Fatal(err)
	}
	if password == "secret" && answer != [2]byte{1, 0} {
		t.Fatal("authentication failed")
	}
	if password != "secret" && answer != [2]byte{1, 1} {
		t.Fatal("bad password accepted")
	}
	return c
}

func testRequest(t *testing.T, c net.Conn, command byte, destination string) string {
	t.Helper()
	_, err := c.Write(append([]byte{5, command, 0}, encodeSOCKSAddress(destination)...))
	if err != nil {
		t.Fatal(err)
	}
	var hdr [3]byte
	if _, err = io.ReadFull(c, hdr[:]); err != nil || hdr != [3]byte{5, 0, 0} {
		t.Fatalf("request: %v %v", hdr, err)
	}
	a, err := readSOCKSAddress(c)
	if err != nil {
		t.Fatal(err)
	}
	return a
}

func TestSOCKSRejectsBadPassword(t *testing.T) {
	a := testBridge(t, (&net.Dialer{}).DialContext)
	c := testControl(t, a, "wrong")
	var b [1]byte
	if _, err := c.Read(b[:]); err == nil {
		t.Fatal("unauthenticated connection remained open")
	}
}

func TestSOCKSTCPAndUDP(t *testing.T) {
	a := testBridge(t, (&net.Dialer{}).DialContext)
	tcp, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer tcp.Close()
	go func() {
		c, e := tcp.Accept()
		if e == nil {
			defer c.Close()
			_, _ = io.Copy(c, c)
		}
	}()
	c := testControl(t, a, "secret")
	testRequest(t, c, 1, tcp.Addr().String())
	message := bytes.Repeat([]byte("qThrone TCP payload"), 700)
	if _, err = c.Write(message); err != nil {
		t.Fatal(err)
	}
	got := make([]byte, len(message))
	if _, err = io.ReadFull(c, got); err != nil || !bytes.Equal(got, message) {
		t.Fatalf("TCP round trip: %v", err)
	}
	udp, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	defer udp.Close()
	go func() {
		b := make([]byte, 2048)
		for {
			n, addr, e := udp.ReadFromUDP(b)
			if e != nil {
				return
			}
			_, _ = udp.WriteToUDP(b[:n], addr)
		}
	}()
	ucontrol := testControl(t, a, "secret")
	bound := testRequest(t, ucontrol, 3, "0.0.0.0:0")
	relay, err := net.Dial("udp", bound)
	if err != nil {
		t.Fatal(err)
	}
	defer relay.Close()
	_ = relay.SetDeadline(time.Now().Add(5 * time.Second))
	frame := append([]byte{0, 0, 0}, encodeSOCKSAddress(udp.LocalAddr().String())...)
	frame = append(frame, []byte("qThrone UDP payload")...)
	_, _ = relay.Write(frame)
	b := make([]byte, 2048)
	n, err := relay.Read(b)
	if err != nil || !bytes.Equal(b[:n], frame) {
		t.Fatalf("UDP round trip: %v", err)
	}
}

func TestSOCKSAddressCodec(t *testing.T) {
	for _, a := range []string{"127.0.0.1:443", "[2001:db8::1]:53", "example.org:80"} {
		b := encodeSOCKSAddress(a)
		decoded, e := readSOCKSAddress(bytes.NewReader(b))
		if e != nil || decoded != a {
			t.Fatalf("%s: %s %v", a, decoded, e)
		}
		for i := 0; i < len(b); i++ {
			if _, e := readSOCKSAddress(bytes.NewReader(b[:i])); e == nil {
				t.Fatalf("accepted truncated address %d", i)
			}
		}
	}
}

// Some servers wait for the request's EOF before sending their response.
// Closing the entire upstream connection on EOF loses that response.
func TestSOCKSTCPHalfClose(t *testing.T) {
	a := testBridge(t, (&net.Dialer{}).DialContext)
	server, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()
	go func() {
		c, e := server.Accept()
		if e != nil {
			return
		}
		defer c.Close()
		_ = c.SetDeadline(time.Now().Add(5 * time.Second))
		request, e := io.ReadAll(c)
		if e == nil {
			_, _ = c.Write(append([]byte("response:"), request...))
		}
	}()
	c := testControl(t, a, "secret")
	testRequest(t, c, 1, server.Addr().String())
	_, _ = c.Write([]byte("request"))
	if err := c.(*net.TCPConn).CloseWrite(); err != nil {
		t.Fatal(err)
	}
	response, err := io.ReadAll(c)
	if err != nil || string(response) != "response:request" {
		t.Fatalf("half-close response: %q %v", response, err)
	}
}

func TestRawConfigValidation(t *testing.T) {
	for _, s := range []string{"RAWCONF:10.200.0.1|1.1.1.1|1280", "RAWCONF:10.200.0.1/24||1280"} {
		if _, _, _, e := parseRawNetConfig(s); e != nil {
			t.Fatal(e)
		}
	}
	for _, s := range []string{"10.200.0.1|1.1.1.1|1280", "RAWCONF:::1|1.1.1.1|1280", "RAWCONF:10.0.0.1|bad|1280", "RAWCONF:10.0.0.1|1.1.1.1|99999"} {
		if _, _, _, e := parseRawNetConfig(s); e == nil {
			t.Fatalf("accepted %s", s)
		}
	}
}

func netstackRoundTrips(t *testing.T, network *netstack.Net, peer *netstack.Net, peerIP string) {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	ln, e := peer.ListenTCP(&net.TCPAddr{IP: net.ParseIP(peerIP), Port: 30080})
	if e != nil {
		t.Fatal(e)
	}
	defer ln.Close()
	go func() {
		c, e := ln.Accept()
		if e == nil {
			defer c.Close()
			_, _ = io.Copy(c, c)
		}
	}()
	c, e := network.DialContext(ctx, "tcp", net.JoinHostPort(peerIP, "30080"))
	if e != nil {
		t.Fatal(e)
	}
	defer c.Close()
	_ = c.SetDeadline(time.Now().Add(5 * time.Second))
	_, e = c.Write([]byte("netstack TCP"))
	if e != nil {
		t.Fatal(e)
	}
	b := make([]byte, 12)
	if _, e = io.ReadFull(c, b); e != nil || string(b) != "netstack TCP" {
		t.Fatalf("netstack TCP: %q %v", b, e)
	}
	u, e := peer.ListenUDP(&net.UDPAddr{IP: net.ParseIP(peerIP), Port: 30053})
	if e != nil {
		t.Fatal(e)
	}
	defer u.Close()
	go func() {
		b := make([]byte, 2048)
		n, a, e := u.ReadFrom(b)
		if e == nil {
			_, _ = u.WriteTo(b[:n], a)
		}
	}()
	d, e := network.DialContext(ctx, "udp", net.JoinHostPort(peerIP, "30053"))
	if e != nil {
		t.Fatal(e)
	}
	defer d.Close()
	_ = d.SetDeadline(time.Now().Add(5 * time.Second))
	_, _ = d.Write([]byte("netstack UDP"))
	n, e := d.Read(b)
	if e != nil || string(b[:n]) != "netstack UDP" {
		t.Fatalf("netstack UDP: %v", e)
	}
}

func TestRAWPacketNetstackTCPAndUDP(t *testing.T) {
	a, an, e := startRawNetstack("RAWCONF:10.200.0.1|1.1.1.1|1280")
	if e != nil {
		t.Fatal(e)
	}
	b, bn, e := startRawNetstack("RAWCONF:10.200.0.2|1.1.1.1|1280")
	if e != nil {
		a.Close()
		t.Fatal(e)
	}
	var wg sync.WaitGroup
	for _, pair := range [][2]*rawPacketDevice{{a, b}, {b, a}} {
		wg.Add(1)
		go func(from, to *rawPacketDevice) {
			defer wg.Done()
			p := make([]byte, 1600)
			for {
				n, e := from.Read(p)
				if e != nil {
					return
				}
				if _, e = to.Write(p[:n]); e != nil {
					return
				}
			}
		}(pair[0], pair[1])
	}
	t.Cleanup(func() { _ = a.Close(); _ = b.Close(); wg.Wait() })
	netstackRoundTrips(t, an, bn, "10.200.0.2")
}

func freeUDPPort(t *testing.T) int {
	t.Helper()
	c, e := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if e != nil {
		t.Fatal(e)
	}
	p := c.LocalAddr().(*net.UDPAddr).Port
	c.Close()
	return p
}

func TestWGNetstackTCPAndUDP(t *testing.T) {
	ka, e := ecdh.X25519().GenerateKey(rand.Reader)
	if e != nil {
		t.Fatal(e)
	}
	kb, e := ecdh.X25519().GenerateKey(rand.Reader)
	if e != nil {
		t.Fatal(e)
	}
	pa, pb := freeUDPPort(t), freeUDPPort(t)
	b64 := base64.StdEncoding.EncodeToString
	conf := func(private, public []byte, address string, port int) string {
		return fmt.Sprintf("[Interface]\nPrivateKey = %s\nAddress = %s/24\n[Peer]\nPublicKey = %s\nAllowedIPs = 10.201.0.0/24\nEndpoint = 127.0.0.1:%d\n", b64(private), address, b64(public), port)
	}
	a, an, e := startUserspaceWireGuard(conf(ka.Bytes(), kb.PublicKey().Bytes(), "10.201.0.1", pb))
	if e != nil {
		t.Fatal(e)
	}
	defer a.Close()
	b, bn, e := startUserspaceWireGuard(conf(kb.Bytes(), ka.PublicKey().Bytes(), "10.201.0.2", pa))
	if e != nil {
		t.Fatal(e)
	}
	defer b.Close()
	if e = a.IpcSet(fmt.Sprintf("listen_port=%d\n", pa)); e != nil {
		t.Fatal(e)
	}
	if e = b.IpcSet(fmt.Sprintf("listen_port=%d\n", pb)); e != nil {
		t.Fatal(e)
	}
	netstackRoundTrips(t, an, bn, "10.201.0.2")
}

func TestPrivateBridgeConfig(t *testing.T) {
	p := filepath.Join(t.TempDir(), "config.json")
	valid := `{"peer":"127.0.0.1:56003","password":"pass","hashes":["hash"],"device_id":"test-device","listen":"127.0.0.1:19000","socks":"127.0.0.1:19001","socks_user":"user","socks_pass":"secret"}`
	if e := os.WriteFile(p, []byte(valid), 0600); e != nil {
		t.Fatal(e)
	}
	c, e := readBridgeConfig(p)
	if e != nil || c.Mode != "raw" {
		t.Fatalf("default mode: %v", e)
	}
	for _, mode := range []string{"raw", "wg"} {
		withWorkers := strings.Replace(valid, `"peer":`, fmt.Sprintf(`"mode":%q,"workers":16,"peer":`, mode), 1)
		if e = os.WriteFile(p, []byte(withWorkers), 0600); e != nil {
			t.Fatal(e)
		}
		c, e = readBridgeConfig(p)
		if e != nil || c.Workers != 9 || c.Mode != mode {
			t.Fatalf("normalize imported workers in %s mode: %v", mode, e)
		}
	}
	for _, bad := range []string{strings.Replace(valid, "127.0.0.1:19001", "0.0.0.0:19001", 1), strings.Replace(valid, `"hash"`, `"a","b","c","d","e"`, 1)} {
		_ = os.WriteFile(p, []byte(bad), 0600)
		if _, e = readBridgeConfig(p); e == nil {
			t.Fatal("invalid config accepted")
		}
	}
}
