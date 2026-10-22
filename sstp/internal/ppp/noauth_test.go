package ppp

import (
	"net"
	"testing"
)

// A NoAuth server and a client that carries no credentials must bring the link
// up with LCP and IPCP alone -- no CHAP challenge, no MS-CHAPv2 -- and the client
// must still receive its assigned address. This is the Fortinet path, where the
// carrier authenticated before PPP ran.
func TestNoAuthHandshake(t *testing.T) {
	w := &wire{}

	clientH := &recordHandler{}
	// No username or password: the client authenticates nowhere at the PPP layer.
	client := New("", "", w.clientTransport(), clientH)

	serverH := &serverRecordHandler{}
	server := NewServer(ServerConfig{
		NoAuth:   true,
		ClientIP: net.IPv4(10, 20, 30, 2),
		ServerIP: net.IPv4(10, 20, 30, 1),
		DNS:      []net.IP{net.IPv4(1, 1, 1, 1)},
	}, w.serverTransport(), serverH)

	client.Start()
	server.Start()
	w.drive(client, server)

	if serverH.authed {
		t.Error("server ran a CHAP exchange despite NoAuth")
	}
	if clientH.authed {
		t.Error("client ran a CHAP exchange despite no auth being requested")
	}
	if !serverH.up || !clientH.up {
		t.Fatalf("network did not come up (server=%v client=%v)", serverH.up, clientH.up)
	}
	if !clientH.cfg.LocalIP.Equal(net.IPv4(10, 20, 30, 2)) {
		t.Errorf("client assigned %v, want 10.20.30.2", clientH.cfg.LocalIP)
	}
	if len(clientH.cfg.DNS) != 1 || !clientH.cfg.DNS[0].Equal(net.IPv4(1, 1, 1, 1)) {
		t.Errorf("client DNS = %v, want [1.1.1.1]", clientH.cfg.DNS)
	}
	if clientH.err != nil || serverH.err != nil {
		t.Errorf("unexpected close: client=%v server=%v", clientH.err, serverH.err)
	}
}

// A NoAuth server must still refuse nothing structural: an authenticating client
// (one whose peer *does* request auth) is the SSTP path and must be unaffected.
// This pins that NoAuth is opt-in and does not leak into the default server.
func TestDefaultServerStillRequiresAuth(t *testing.T) {
	w := &wire{}
	clientH := &recordHandler{}
	client := New("alice", "pw", w.clientTransport(), clientH)
	serverH := &serverRecordHandler{}
	server := NewServer(ServerConfig{
		ClientIP: net.IPv4(10, 0, 0, 5),
		ServerIP: net.IPv4(10, 0, 0, 1),
		Auth:     func(u string) (string, bool) { return "pw", u == "alice" },
	}, w.serverTransport(), serverH)

	client.Start()
	server.Start()
	w.drive(client, server)

	if !serverH.authed {
		t.Error("the default server did not run MS-CHAPv2 (NoAuth leaked into the default)")
	}
	if !serverH.up || !clientH.up {
		t.Error("authenticated link did not come up")
	}
}
