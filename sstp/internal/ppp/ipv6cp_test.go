package ppp

import (
	"bytes"
	"errors"
	"net"
	"sync"
	"testing"
	"time"

	"swiftsstp/internal/mschap"
)

// ipv6Handler records the session's events and signals NetworkUp/Closed on
// channels, so tests can wait without racing the Restart timer, which fires on
// its own goroutine.
type ipv6Handler struct {
	mu       sync.Mutex
	cfg      IPConfig
	err      error
	ups      int
	upCh     chan struct{}
	closedCh chan struct{}
}

func newIPv6Handler() *ipv6Handler {
	return &ipv6Handler{
		upCh:     make(chan struct{}, 1),
		closedCh: make(chan struct{}, 1),
	}
}

func (h *ipv6Handler) Authenticated(nt [mschap.NTResponseLen]byte) {}

func (h *ipv6Handler) NetworkUp(cfg IPConfig) {
	h.mu.Lock()
	h.cfg = cfg
	h.ups++
	h.mu.Unlock()
	select {
	case h.upCh <- struct{}{}:
	default:
	}
}

func (h *ipv6Handler) Closed(err error) {
	h.mu.Lock()
	if h.err == nil {
		h.err = err
	}
	h.mu.Unlock()
	select {
	case h.closedCh <- struct{}{}:
	default:
	}
}

func (h *ipv6Handler) config() IPConfig {
	h.mu.Lock()
	defer h.mu.Unlock()
	return h.cfg
}

func (h *ipv6Handler) closed() error {
	h.mu.Lock()
	defer h.mu.Unlock()
	return h.err
}

func (h *ipv6Handler) upCount() int {
	h.mu.Lock()
	defer h.mu.Unlock()
	return h.ups
}

// waitUp blocks until the link reports up, failing the test if it closes or
// stalls instead.
func (h *ipv6Handler) waitUp(t *testing.T) {
	t.Helper()
	select {
	case <-h.upCh:
	case <-h.closedCh:
		t.Fatalf("link closed instead of coming up: %v", h.closed())
	case <-time.After(5 * time.Second):
		t.Fatal("timed out waiting for NetworkUp")
	}
}

// runToIPv6Phase drives the scripted server through LCP, MS-CHAPv2 and IPCP
// up to the point where IPv6CP starts. On return, with SetIPv6(true) the link
// is in the IPv6CP phase and the client's IPv6CP Configure-Request is the most
// recent frame it sent.
func runToIPv6Phase(t *testing.T, s *Session, tr *captureTransport) {
	t.Helper()
	const username, password = "User", "pass"

	s.Start()
	req := tr.lastCP(t, ProtocolLCP)
	deliver(s, ProtocolLCP, codeConfigureAck, req.ID, req.Body)
	deliver(s, ProtocolLCP, codeConfigureRequest, 1, marshalOptions([]option{
		{Type: optMRU, Value: []byte{0x05, 0xdc}},
		{Type: optAuthProto, Value: authMSCHAPv2},
	}))

	var authCh [16]byte
	for i := range authCh {
		authCh[i] = byte(i + 1)
	}
	deliver(s, ProtocolCHAP, chapChallenge, 2, append([]byte{16}, authCh[:]...))
	resp := tr.lastCP(t, ProtocolCHAP)
	var peerCh [16]byte
	var ntResp [mschap.NTResponseLen]byte
	copy(peerCh[:], resp.Body[1:17])
	copy(ntResp[:], resp.Body[25:49])
	authString := mschap.AuthenticatorResponse(authCh, peerCh, username, password, ntResp)
	deliver(s, ProtocolCHAP, chapSuccess, 2, []byte(authString+" M=Welcome"))

	ipcpReq := tr.lastCP(t, ProtocolIPCP)
	nak := marshalOptions([]option{
		{Type: optIPAddress, Value: []byte{10, 0, 0, 5}},
		{Type: optPrimaryDNS, Value: []byte{8, 8, 8, 8}},
	})
	deliver(s, ProtocolIPCP, codeConfigureNak, ipcpReq.ID, nak)
	ipcpReq2 := tr.lastCP(t, ProtocolIPCP)
	deliver(s, ProtocolIPCP, codeConfigureAck, ipcpReq2.ID, ipcpReq2.Body)
	deliver(s, ProtocolIPCP, codeConfigureRequest, 20, marshalOptions([]option{
		{Type: optIPAddress, Value: []byte{10, 0, 0, 1}},
	}))
}

// fastIPv6Budget shrinks the IPv6CP request budget so a silent peer's give-up
// is observable in a test.
func fastIPv6Budget(t *testing.T) {
	t.Helper()
	old := ipv6CPRestartInterval
	ipv6CPRestartInterval = 20 * time.Millisecond
	t.Cleanup(func() { ipv6CPRestartInterval = old })
}

// countIPv6CP frames the client has sent.
func countIPv6CP(tr *captureTransport) int {
	n := 0
	for _, f := range tr.frames {
		if proto, _, ok := decodeFrame(f); ok && proto == ProtocolIPv6CP {
			n++
		}
	}
	return n
}

var testPeerIID = []byte{0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01}

// TestIPv6CPCodec covers the IPv6CP packet and option encoding: a Configure-
// Request carrying one Interface-Identifier round-trips through marshal and
// parse, and a truncated packet is rejected rather than misread.
func TestIPv6CPCodec(t *testing.T) {
	iid := []byte{0x02, 1, 2, 3, 4, 5, 6, 7}
	pkt := cpPacket{Code: codeConfigureRequest, ID: 7, Body: marshalOptions([]option{{Type: optInterfaceIdentifier, Value: iid}})}
	wire := encodeFrame(ProtocolIPv6CP, pkt.marshal())

	proto, payload, ok := decodeFrame(wire)
	if !ok || proto != ProtocolIPv6CP {
		t.Fatalf("frame protocol = %#x, want %#x", proto, ProtocolIPv6CP)
	}
	got, ok := parseCP(payload)
	if !ok || got.Code != codeConfigureRequest || got.ID != 7 {
		t.Fatalf("parseCP = %+v ok=%v", got, ok)
	}
	opts, ok := parseOptions(got.Body)
	if !ok || len(opts) != 1 || opts[0].Type != optInterfaceIdentifier || !bytes.Equal(opts[0].Value, iid) {
		t.Fatalf("options = %+v, want one Interface-Identifier %x", opts, iid)
	}
	// A length that runs off the end of the buffer is malformed.
	if _, ok := parseCP(payload[:len(payload)-1]); ok {
		t.Error("parseCP accepted a truncated IPv6CP packet")
	}
}

// TestRandomIID checks the locally generated identifier: 8 octets, non-zero,
// universal/local bit set and group bit clear per RFC 5072 section 5.7, and
// different every time.
func TestRandomIID(t *testing.T) {
	seen := make(map[string]bool)
	for i := 0; i < 100; i++ {
		iid := randomIID()
		if len(iid) != ipv6IIDLen {
			t.Fatalf("IID length = %d, want %d", len(iid), ipv6IIDLen)
		}
		if isZeroIID(iid) {
			t.Fatal("IID is all zeros")
		}
		if iid[0]&0x02 == 0 {
			t.Error("universal/local bit not set")
		}
		if iid[0]&0x01 != 0 {
			t.Error("group bit set")
		}
		if seen[string(iid)] {
			t.Fatal("duplicate IID across 100 draws")
		}
		seen[string(iid)] = true
	}
}

// TestIPv6CPNegotiation is the happy path: the client proposes a random
// identifier, adopts the server's Configure-Nak suggestion, and the link comes
// up with both IIDs and the IPv4 configuration intact.
func TestIPv6CPNegotiation(t *testing.T) {
	tr := &captureTransport{}
	h := newIPv6Handler()
	s := New("User", "pass", tr, h)
	s.SetIPv6(true)
	runToIPv6Phase(t, s, tr)

	if h.upCount() != 0 {
		t.Fatal("link came up before IPv6CP finished")
	}

	// The client opens IPv6CP with a usable, locally assigned identifier.
	req := tr.lastCP(t, ProtocolIPv6CP)
	if req.Code != codeConfigureRequest {
		t.Fatalf("IPv6CP code = %d, want Configure-Request", req.Code)
	}
	opts, ok := parseOptions(req.Body)
	if !ok || len(opts) != 1 || opts[0].Type != optInterfaceIdentifier || len(opts[0].Value) != ipv6IIDLen {
		t.Fatalf("IPv6CP request body = %x, want one 8-octet Interface-Identifier", req.Body)
	}
	if isZeroIID(opts[0].Value) {
		t.Error("proposed IID is all zeros")
	}
	if opts[0].Value[0]&0x02 == 0 || opts[0].Value[0]&0x01 != 0 {
		t.Errorf("proposed IID %x is not a unicast locally assigned identifier", opts[0].Value)
	}

	// The server Naks with a suggestion of its own; the client must adopt it.
	suggestion := []byte{0x02, 0, 0, 0, 0, 0, 0, 1}
	nakBody := marshalOptions([]option{{Type: optInterfaceIdentifier, Value: suggestion}})
	deliver(s, ProtocolIPv6CP, codeConfigureNak, req.ID, nakBody)
	resent := tr.lastCP(t, ProtocolIPv6CP)
	if resent.Code != codeConfigureRequest {
		t.Fatalf("after a Nak the client must re-request, got code %d", resent.Code)
	}
	ropts, ok := parseOptions(resent.Body)
	if !ok || len(ropts) != 1 || !bytes.Equal(ropts[0].Value, suggestion) {
		t.Fatalf("resent IID = %x, want the server's suggestion %x", ropts, suggestion)
	}
	deliver(s, ProtocolIPv6CP, codeConfigureAck, resent.ID, resent.Body)

	// The server opens its own side with a usable identifier and the link
	// comes up with both.
	deliver(s, ProtocolIPv6CP, codeConfigureRequest, 30, marshalOptions([]option{
		{Type: optInterfaceIdentifier, Value: testPeerIID},
	}))
	ack := tr.lastCP(t, ProtocolIPv6CP)
	if ack.Code != codeConfigureAck || ack.ID != 30 {
		t.Fatalf("expected IPv6CP Ack for the server's request, got code %d id %d", ack.Code, ack.ID)
	}

	h.waitUp(t)
	cfg := h.config()
	if !bytes.Equal(cfg.LocalIP6, suggestion) {
		t.Errorf("LocalIP6 = %x, want the adopted suggestion %x", cfg.LocalIP6, suggestion)
	}
	if !bytes.Equal(cfg.PeerIP6, testPeerIID) {
		t.Errorf("PeerIP6 = %x, want %x", cfg.PeerIP6, testPeerIID)
	}
	if !cfg.LocalIP.Equal(net.IPv4(10, 0, 0, 5)) || !cfg.PeerIP.Equal(net.IPv4(10, 0, 0, 1)) {
		t.Errorf("IPv4 addresses lost: local %v peer %v", cfg.LocalIP, cfg.PeerIP)
	}
	if len(cfg.DNS) != 1 || !cfg.DNS[0].Equal(net.IPv4(8, 8, 8, 8)) {
		t.Errorf("DNS = %v, want [8.8.8.8]", cfg.DNS)
	}
	if err := h.closed(); err != nil {
		t.Errorf("unexpected Closed(%v)", err)
	}
}

// TestIPv6CPRejectDegradesToIPv4: a server that rejects the Interface-
// Identifier gets a bare re-request (RFC 1661 section 6.3); once both sides
// open anyway the link comes up on IPv4 with no IPv6 identifiers — graceful
// degradation, which is the SoftEther story.
func TestIPv6CPRejectDegradesToIPv4(t *testing.T) {
	tr := &captureTransport{}
	h := newIPv6Handler()
	s := New("User", "pass", tr, h)
	s.SetIPv6(true)
	runToIPv6Phase(t, s, tr)

	req := tr.lastCP(t, ProtocolIPv6CP)
	deliver(s, ProtocolIPv6CP, codeConfigureReject, req.ID, req.Body)

	resent := tr.lastCP(t, ProtocolIPv6CP)
	if resent.Code != codeConfigureRequest {
		t.Fatalf("after a Reject the client must re-request, got code %d", resent.Code)
	}
	ropts, ok := parseOptions(resent.Body)
	if !ok || len(ropts) != 0 {
		t.Fatalf("after a Reject the IID must be dropped, got body %x", resent.Body)
	}
	deliver(s, ProtocolIPv6CP, codeConfigureAck, resent.ID, resent.Body)
	deliver(s, ProtocolIPv6CP, codeConfigureRequest, 30, marshalOptions([]option{
		{Type: optInterfaceIdentifier, Value: testPeerIID},
	}))

	h.waitUp(t)
	cfg := h.config()
	if cfg.LocalIP6 != nil || cfg.PeerIP6 != nil {
		t.Errorf("link negotiated IPv6 despite a rejected IID: local %v peer %v", cfg.LocalIP6, cfg.PeerIP6)
	}
	if !cfg.LocalIP.Equal(net.IPv4(10, 0, 0, 5)) {
		t.Errorf("IPv4 addressing lost: %v", cfg.LocalIP)
	}
	if err := h.closed(); err != nil {
		t.Errorf("unexpected Closed(%v)", err)
	}
}

// TestIPv6CPNaksAllZeroPeerSuggestion: RFC 5072 section 5 — a peer that
// requests with an all-zero identifier is asking to be suggested one, so the
// client Configure-Naks with its own instead of acking a peer with no
// identifier on the link.
func TestIPv6CPNaksAllZeroPeerSuggestion(t *testing.T) {
	tr := &captureTransport{}
	h := newIPv6Handler()
	s := New("User", "pass", tr, h)
	s.SetIPv6(true)
	runToIPv6Phase(t, s, tr)

	req := tr.lastCP(t, ProtocolIPv6CP)
	ropts, ok := parseOptions(req.Body)
	if !ok || len(ropts) != 1 {
		t.Fatalf("IPv6CP request body = %x, want one option", req.Body)
	}
	ours := append([]byte(nil), ropts[0].Value...)

	deliver(s, ProtocolIPv6CP, codeConfigureRequest, 30, marshalOptions([]option{
		{Type: optInterfaceIdentifier, Value: make([]byte, ipv6IIDLen)},
	}))
	nak := tr.lastCP(t, ProtocolIPv6CP)
	if nak.Code != codeConfigureNak || nak.ID != 30 {
		t.Fatalf("expected Configure-Nak for the all-zero request, got code %d id %d", nak.Code, nak.ID)
	}
	nopts, ok := parseOptions(nak.Body)
	if !ok || len(nopts) != 1 || isZeroIID(nopts[0].Value) {
		t.Fatalf("Nak must suggest a usable IID, got body %x", nak.Body)
	}
	if !bytes.Equal(nopts[0].Value, ours) {
		t.Errorf("suggestion = %x, want our own IID %x", nopts[0].Value, ours)
	}

	// With real identifiers on both sides the link comes up negotiated.
	deliver(s, ProtocolIPv6CP, codeConfigureAck, req.ID, req.Body)
	deliver(s, ProtocolIPv6CP, codeConfigureRequest, 31, marshalOptions([]option{
		{Type: optInterfaceIdentifier, Value: testPeerIID},
	}))
	h.waitUp(t)
	cfg := h.config()
	if !bytes.Equal(cfg.LocalIP6, ours) || !bytes.Equal(cfg.PeerIP6, testPeerIID) {
		t.Errorf("IPv6 identifiers = local %x peer %x, want %x / %x", cfg.LocalIP6, cfg.PeerIP6, ours, testPeerIID)
	}
}

// TestIPv6CPSilentServerDegrades: a peer that never answers IPv6CP does not
// implement it, so the client gives up after its request budget and the link
// comes up on IPv4. The requests are retransmitted within the budget first.
func TestIPv6CPSilentServerDegrades(t *testing.T) {
	fastIPv6Budget(t)
	tr := &captureTransport{}
	h := newIPv6Handler()
	s := New("User", "pass", tr, h)
	s.SetIPv6(true)
	runToIPv6Phase(t, s, tr)

	h.waitUp(t)
	if got := countIPv6CP(tr); got != maxIPv6Configure {
		t.Errorf("IPv6CP requests sent = %d, want the full budget of %d", got, maxIPv6Configure)
	}
	cfg := h.config()
	if cfg.LocalIP6 != nil || cfg.PeerIP6 != nil {
		t.Errorf("link negotiated IPv6 against a silent peer: local %v peer %v", cfg.LocalIP6, cfg.PeerIP6)
	}
	if !cfg.LocalIP.Equal(net.IPv4(10, 0, 0, 5)) {
		t.Errorf("IPv4 addressing lost: %v", cfg.LocalIP)
	}
	if err := h.closed(); err != nil {
		t.Errorf("unexpected Closed(%v)", err)
	}
}

// TestIPv6CPStrictRejectFailsLink: with SetIPv6Strict the same rejection that
// degrades above kills the link instead — the caller demanded IPv6.
func TestIPv6CPStrictRejectFailsLink(t *testing.T) {
	tr := &captureTransport{}
	h := newIPv6Handler()
	s := New("User", "pass", tr, h)
	s.SetIPv6(true)
	s.SetIPv6Strict(true)
	runToIPv6Phase(t, s, tr)

	req := tr.lastCP(t, ProtocolIPv6CP)
	deliver(s, ProtocolIPv6CP, codeConfigureReject, req.ID, req.Body)
	resent := tr.lastCP(t, ProtocolIPv6CP)
	deliver(s, ProtocolIPv6CP, codeConfigureAck, resent.ID, resent.Body)
	deliver(s, ProtocolIPv6CP, codeConfigureRequest, 30, marshalOptions([]option{
		{Type: optInterfaceIdentifier, Value: testPeerIID},
	}))

	select {
	case <-h.upCh:
		t.Fatal("strict link came up without a negotiated IID")
	default:
	}
	if err := h.closed(); !errors.Is(err, ErrIPv6Negotiation) {
		t.Fatalf("Closed error = %v, want one satisfying errors.Is(err, ErrIPv6Negotiation)", err)
	}
	if h.upCount() != 0 {
		t.Error("NetworkUp fired on a strict link that failed IPv6CP")
	}
}

// TestIPv6CPStrictSilentServerFailsLink: silence is also a failed IPv6
// negotiation when the link is strict.
func TestIPv6CPStrictSilentServerFailsLink(t *testing.T) {
	fastIPv6Budget(t)
	tr := &captureTransport{}
	h := newIPv6Handler()
	s := New("User", "pass", tr, h)
	s.SetIPv6(true)
	s.SetIPv6Strict(true)
	runToIPv6Phase(t, s, tr)

	select {
	case <-h.closedCh:
	case <-time.After(5 * time.Second):
		t.Fatal("timed out waiting for the strict link to fail")
	}
	if err := h.closed(); !errors.Is(err, ErrIPv6Negotiation) {
		t.Fatalf("Closed error = %v, want ErrIPv6Negotiation", err)
	}
	if h.upCount() != 0 {
		t.Error("NetworkUp fired on a strict link that failed IPv6CP")
	}
}

// TestIPv6DisabledIgnoresIPv6CPFrames pins the default: without SetIPv6 the
// link is exactly the IPv4 link it always was — up as soon as IPCP settles,
// and IPv6CP frames from the server are ignored, not answered.
func TestIPv6DisabledIgnoresIPv6CPFrames(t *testing.T) {
	tr := &captureTransport{}
	h := newIPv6Handler()
	s := New("User", "pass", tr, h)
	runToIPv6Phase(t, s, tr)

	h.waitUp(t)
	sent := len(tr.frames)
	deliver(s, ProtocolIPv6CP, codeConfigureRequest, 40, marshalOptions([]option{
		{Type: optInterfaceIdentifier, Value: testPeerIID},
	}))
	if len(tr.frames) != sent {
		t.Errorf("client answered IPv6CP while disabled (%d extra frames)", len(tr.frames)-sent)
	}
	if got := countIPv6CP(tr); got != 0 {
		t.Errorf("client sent %d IPv6CP frames while disabled", got)
	}
	cfg := h.config()
	if cfg.LocalIP6 != nil || cfg.PeerIP6 != nil {
		t.Errorf("disabled link reported IPv6: %v / %v", cfg.LocalIP6, cfg.PeerIP6)
	}
	if !cfg.LocalIP.Equal(net.IPv4(10, 0, 0, 5)) {
		t.Errorf("IPv4 addressing wrong: %v", cfg.LocalIP)
	}
}
