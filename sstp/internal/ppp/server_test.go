package ppp

import (
	"context"
	"errors"
	"net"
	"sync"
	"testing"
	"time"

	"swiftsstp/internal/mschap"
)

// transportFunc adapts a function to the Transport interface.
type transportFunc func([]byte) error

func (f transportFunc) SendPPP(b []byte) error { return f(b) }

// wire is an in-memory full-duplex link between a client and server session: each
// side's sends queue for the other, and a driver delivers them in rounds until
// the exchange goes quiet.
type wire struct {
	mu               sync.Mutex
	toClient, toServ [][]byte
}

func (w *wire) clientTransport() Transport {
	return transportFunc(func(b []byte) error {
		w.mu.Lock()
		w.toServ = append(w.toServ, append([]byte(nil), b...))
		w.mu.Unlock()
		return nil
	})
}

func (w *wire) serverTransport() Transport {
	return transportFunc(func(b []byte) error {
		w.mu.Lock()
		w.toClient = append(w.toClient, append([]byte(nil), b...))
		w.mu.Unlock()
		return nil
	})
}

func (w *wire) drive(client *Session, server *ServerSession) {
	for range 200 {
		w.mu.Lock()
		cs, cc := w.toServ, w.toClient
		w.toServ, w.toClient = nil, nil
		w.mu.Unlock()
		if len(cs) == 0 && len(cc) == 0 {
			return
		}
		for _, f := range cs {
			server.Receive(f)
		}
		for _, f := range cc {
			client.Receive(f)
		}
	}
}

type serverRecordHandler struct {
	username   string
	ntResponse [mschap.NTResponseLen]byte
	authed, up bool
	err        error
}

func (h *serverRecordHandler) Authenticated(u, _ string, nt [mschap.NTResponseLen]byte) {
	h.authed, h.username, h.ntResponse = true, u, nt
}
func (h *serverRecordHandler) NetworkUp()       { h.up = true }
func (h *serverRecordHandler) Closed(err error) { h.err = err }

// TestServerClientHandshake drives the veepin PPP client against the veepin PPP
// server over an in-memory link and checks the whole flow converges: LCP with
// MS-CHAPv2, a verified authentication in both directions, and IPCP assigning the
// client the server's chosen address and DNS.
func TestServerClientHandshake(t *testing.T) {
	const username, password = "alice", "s3cret"
	w := &wire{}

	clientH := &recordHandler{}
	client := New(username, password, w.clientTransport(), clientH)

	serverH := &serverRecordHandler{}
	server := NewServer(ServerConfig{
		ClientIP: net.IPv4(10, 0, 0, 5),
		ServerIP: net.IPv4(10, 0, 0, 1),
		DNS:      []net.IP{net.IPv4(8, 8, 8, 8)},
		Auth: func(u string) (string, bool) {
			if u == username {
				return password, true
			}
			return "", false
		},
	}, w.serverTransport(), serverH)

	client.Start()
	server.Start()
	w.drive(client, server)

	if !serverH.authed {
		t.Fatal("server did not authenticate the client")
	}
	if serverH.username != username {
		t.Errorf("server authenticated %q, want %q", serverH.username, username)
	}
	if !clientH.authed {
		t.Fatal("client did not see authentication succeed")
	}
	if serverH.ntResponse != clientH.ntResponse {
		t.Error("server and client disagree on the NT response")
	}
	if !serverH.up || !clientH.up {
		t.Fatalf("network did not come up (server=%v client=%v)", serverH.up, clientH.up)
	}
	if !clientH.cfg.LocalIP.Equal(net.IPv4(10, 0, 0, 5)) {
		t.Errorf("client assigned %v, want 10.0.0.5", clientH.cfg.LocalIP)
	}
	if len(clientH.cfg.DNS) != 1 || !clientH.cfg.DNS[0].Equal(net.IPv4(8, 8, 8, 8)) {
		t.Errorf("client DNS = %v, want [8.8.8.8]", clientH.cfg.DNS)
	}
	if clientH.err != nil || serverH.err != nil {
		t.Errorf("unexpected close: client=%v server=%v", clientH.err, serverH.err)
	}
}

// TestServerRejectsWrongPassword checks the server fails a client whose password
// does not match, and the client sees the failure.
func TestServerRejectsWrongPassword(t *testing.T) {
	w := &wire{}
	clientH := &recordHandler{}
	client := New("alice", "wrong", w.clientTransport(), clientH)
	serverH := &serverRecordHandler{}
	server := NewServer(ServerConfig{
		ClientIP: net.IPv4(10, 0, 0, 5),
		ServerIP: net.IPv4(10, 0, 0, 1),
		Auth:     func(string) (string, bool) { return "correct", true },
	}, w.serverTransport(), serverH)

	client.Start()
	server.Start()
	w.drive(client, server)

	if serverH.up || clientH.up {
		t.Fatal("network came up despite a wrong password")
	}
	if serverH.err == nil {
		t.Error("server did not report an auth failure")
	}
	if clientH.err == nil {
		t.Error("client did not report an auth failure")
	}
}

// echoPair brings a client and server session up over the in-memory wire, so a
// test about liveness does not restate the handshake.
func echoPair(t *testing.T) (*wire, *Session, *ServerSession) {
	t.Helper()
	const username, password = "alice", "s3cret"
	w := &wire{}
	client := New(username, password, w.clientTransport(), &recordHandler{})
	server := NewServer(ServerConfig{
		ClientIP: net.IPv4(10, 0, 0, 5),
		ServerIP: net.IPv4(10, 0, 0, 1),
		Auth:     func(u string) (string, bool) { return password, u == username },
	}, w.serverTransport(), &serverRecordHandler{})
	client.Start()
	server.Start()
	w.drive(client, server)
	return w, client, server
}

// TestAnEchoRequestIsAnsweredByTheServer. This package has always answered a
// peer's LCP Echo-Request and never sent one, which proves this end alive to a
// peer and learns nothing about the peer -- the wrong half to have on a client.
// Fortinet rides this layer and prefers a DTLS carrier that can stop delivering
// without ever erroring, so the asking half is what tells it the carrier died.
func TestAnEchoRequestIsAnsweredByTheServer(t *testing.T) {
	w, client, server := echoPair(t)

	done := make(chan error, 1)
	go func() { done <- client.SendEcho(context.Background()) }()

	// Let the request reach the server and the reply come back. drive is
	// synchronous, so it is run until the echo resolves rather than once.
	deadline := time.After(2 * time.Second)
	for {
		select {
		case err := <-done:
			if err != nil {
				t.Fatalf("SendEcho: %v", err)
			}
			return
		case <-deadline:
			t.Fatal("the server never answered an LCP Echo-Request")
		default:
			w.drive(client, server)
			time.Sleep(time.Millisecond)
		}
	}
}

// TestAnEchoNobodyAnswersDoesNotHangForever. The liveness monitor bounds each
// probe with a context; this asserts the bound is honoured rather than the
// caller being stuck on a channel nothing will close.
func TestAnEchoNobodyAnswersDoesNotHangForever(t *testing.T) {
	w := &wire{}
	// A client with no peer at all: frames queue on the wire and nothing
	// consumes them, which is exactly a black-holed carrier.
	client := New("alice", "s3cret", w.clientTransport(), &recordHandler{})
	client.Start()

	ctx, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
	defer cancel()
	if err := client.SendEcho(ctx); !errors.Is(err, context.DeadlineExceeded) {
		t.Errorf("SendEcho against a silent peer = %v, want a deadline", err)
	}
}

// TestAnEchoOnAClosedLinkSaysSoImmediately. A link that has already declared
// itself dead must release a waiting probe rather than making it wait out the
// context, so a torn-down session does not cost the caller a timeout it has
// already earned the answer to.
func TestAnEchoOnAClosedLinkSaysSoImmediately(t *testing.T) {
	_, client, _ := echoPair(t)
	// Tear the link down the way a transport failure does: there is no public
	// Close on a client session, because the carrier's owner closes the carrier
	// and the session learns from the send that follows.
	client.withLock(func() { client.failLocked(errors.New("test: carrier gone")) })

	start := time.Now()
	if err := client.SendEcho(context.Background()); !errors.Is(err, ErrLinkClosed) {
		t.Errorf("SendEcho on a closed link = %v, want ErrLinkClosed", err)
	}
	if elapsed := time.Since(start); elapsed > time.Second {
		t.Errorf("SendEcho on a closed link took %v, want an immediate answer", elapsed)
	}
}
