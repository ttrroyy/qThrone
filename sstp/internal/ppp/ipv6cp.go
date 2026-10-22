package ppp

import (
	"crypto/rand"
	"errors"
	"time"
)

// IPv6CP (RFC 5072) is IPv6's Network Control Protocol: a Configuration
// Protocol per RFC 1661, exchanging the Interface-Identifier option instead of
// addresses. Each side proposes the 64-bit identifier it will use on the link,
// from which it forms its own address (usually fe80:: plus the IID); no
// addresses or DNS change hands.
//
// No server support is assumed. SoftEther and most SSTP deployments never
// answer IPv6CP, so the client asks, and if the peer stays silent through a
// small request budget — on a reliable transport silence means "not
// implemented", not "packet lost" — the negotiation is abandoned and the link
// comes up carrying IPv4 alone, exactly as it did before IPv6CP existed. A
// Configure-Reject of the Interface-Identifier drops the option and
// re-requests (RFC 1661 section 6.3), which lands in the same place: an
// IPv4-only link, reported to the caller with no IPv6 identifiers. SetIPv6Strict
// turns either outcome into a dead link instead.

// optInterfaceIdentifier is IPv6CP's only option type (RFC 5072 section 5).
const optInterfaceIdentifier = 1

// ipv6IIDLen is the length in bytes of an Interface-Identifier.
const ipv6IIDLen = 8

// ipv6CPRestartInterval and maxIPv6Configure bound the IPv6CP Configure-Request
// budget. They are deliberately tighter than LCP's and IPCP's (restart.go):
// those retransmit for L2TP's unreliable carrier, while IPv6CP only ever runs
// against a server that answers — one that does not should cost the link a
// moment, not the restartInterval x maxConfigure stall. The interval is a var
// so tests can shrink it.
var (
	ipv6CPRestartInterval = time.Second
	maxIPv6Configure      = 3
)

// ErrIPv6Negotiation reports a link configured strict about IPv6 whose IPv6CP
// negotiation did not complete — the peer rejected the option or never
// answered. It satisfies errors.Is, so callers can separate "the server has no
// IPv6" from a transport fault.
var ErrIPv6Negotiation = errors.New("ppp: IPv6CP negotiation did not complete")

// randomIID returns a locally assigned interface identifier: random bytes with
// the universal/local bit set and the group bit cleared, as RFC 5072 section
// 5.7 requires of a software-generated IID.
func randomIID() []byte {
	var iid [ipv6IIDLen]byte
	_, _ = rand.Read(iid[:])
	iid[0] |= 0x02 // universal/local: this identifier is locally assigned
	iid[0] &= 0xfe // group bit clear: it names an interface, not a group
	return iid[:]
}

// isZeroIID reports whether iid is absent or all zeros — RFC 5072's "I have no
// interface identifier, suggest one".
func isZeroIID(iid []byte) bool {
	for _, b := range iid {
		if b != 0 {
			return false
		}
	}
	return true
}

// startIPv6CP opens IPv6CP with this side's interface identifier, generating
// one if the peer's early requests have not already forced the choice.
func (s *Session) startIPv6CP() {
	if len(s.reqIID) != ipv6IIDLen {
		s.reqIID = randomIID()
	}
	s.sendIPv6CPConfigReq()
}

func (s *Session) sendIPv6CPConfigReq() {
	var opts []option
	// A rejected-and-dropped IID (see dropRejectedIPv6CP) leaves nothing to
	// request; the bare Configure-Request is RFC 1661's "no options" form.
	if len(s.reqIID) == ipv6IIDLen {
		opts = append(opts, option{Type: optInterfaceIdentifier, Value: s.reqIID})
	}
	s.ipv6ReqID = s.nextID()
	s.ipv6ConfigReq = cpPacket{Code: codeConfigureRequest, ID: s.ipv6ReqID, Body: marshalOptions(opts)}.marshal()
	s.resendIPv6CPConfigReq()
}

// resendIPv6CPConfigReq (re)transmits the outstanding IPv6CP Configure-Request
// and re-arms its (short) Restart timer, reusing the original identifier.
func (s *Session) resendIPv6CPConfigReq() {
	s.send(ProtocolIPv6CP, s.ipv6ConfigReq)
	s.ipv6Restart.armCustom(ipv6CPRestartInterval, maxIPv6Configure, s.withLock, s.resendIPv6CPConfigReq, func() {
		s.ipv6GiveUpLocked()
	})
}

// handleIPv6CP processes one inbound IPv6CP packet. Frames arriving while
// IPv6CP is disabled are ignored rather than rejected: a client that does not
// implement an NCP simply does not answer it, and the peer's own budget ends
// the exchange.
func (s *Session) handleIPv6CP(payload []byte) {
	if !s.ipv6Enabled {
		return
	}
	pkt, ok := parseCP(payload)
	if !ok {
		return
	}
	// Any reply keeps the IPv6CP request budget fresh, as for IPCP.
	s.ipv6Restart.alive()
	switch pkt.Code {
	case codeConfigureRequest:
		// Answered in any post-auth phase: a peer that opens IPv6CP while IPCP
		// is still settling gets its request acked and its identifier recorded,
		// and the link picks both up when IPv6CP becomes the active phase.
		s.handleIPv6CPConfigReq(pkt)
	case codeConfigureAck:
		if pkt.ID == s.ipv6ReqID {
			s.ipv6Restart.stop()
			s.ipv6LocalOpen = true
			s.maybeIPv6Up()
		}
	case codeConfigureNak:
		// The peer suggests an identifier of its own choosing; adopting it is
		// how a server that wants to assign ours steers the exchange (the same
		// convergence IPCP uses for addresses).
		if s.phase == phaseIPv6CP && pkt.ID == s.ipv6ReqID {
			s.adoptIPv6CPValues(pkt.Body)
			s.sendIPv6CPConfigReq()
		}
	case codeConfigureReject:
		if s.phase == phaseIPv6CP && pkt.ID == s.ipv6ReqID {
			s.dropRejectedIPv6CP(pkt.Body)
			s.sendIPv6CPConfigReq()
		}
	}
}

// handleIPv6CPConfigReq answers the peer's IPv6CP Configure-Request. An
// all-zero (or missing) identifier is the peer asking to be suggested one, so
// it draws a Configure-Nak carrying ours; a usable identifier is recorded as
// the peer's and acked verbatim.
func (s *Session) handleIPv6CPConfigReq(pkt cpPacket) {
	opts, ok := parseOptions(pkt.Body)
	if !ok {
		return
	}
	var iid []byte
	for _, o := range opts {
		if o.Type == optInterfaceIdentifier && len(o.Value) == ipv6IIDLen {
			iid = append([]byte(nil), o.Value...)
			break
		}
	}
	if isZeroIID(iid) {
		if len(s.reqIID) != ipv6IIDLen {
			s.reqIID = randomIID()
		}
		nak := marshalOptions([]option{{Type: optInterfaceIdentifier, Value: s.reqIID}})
		s.send(ProtocolIPv6CP, cpPacket{Code: codeConfigureNak, ID: pkt.ID, Body: nak}.marshal())
		return
	}
	s.peerIID = iid
	s.send(ProtocolIPv6CP, cpPacket{Code: codeConfigureAck, ID: pkt.ID, Body: pkt.Body}.marshal())
	s.ipv6RemoteOpen = true
	s.maybeIPv6Up()
}

// adoptIPv6CPValues takes the interface identifier the peer suggested in a
// Configure-Nak and resubmits it as ours, mirroring adoptIPCPValues.
func (s *Session) adoptIPv6CPValues(body []byte) {
	opts, ok := parseOptions(body)
	if !ok {
		return
	}
	for _, o := range opts {
		if o.Type == optInterfaceIdentifier && len(o.Value) == ipv6IIDLen && !isZeroIID(o.Value) {
			s.reqIID = append([]byte(nil), o.Value...)
		}
	}
}

// dropRejectedIPv6CP removes options the peer refused to accept. The only
// IPv6CP option is the Interface-Identifier, so dropping it leaves a bare
// request: if that is acked the exchange "completes" with no identifiers and
// the link comes up without IPv6 (or fails, when strict).
func (s *Session) dropRejectedIPv6CP(body []byte) {
	opts, ok := parseOptions(body)
	if !ok {
		return
	}
	for _, o := range opts {
		if o.Type == optInterfaceIdentifier {
			s.reqIID = nil
		}
	}
}

func (s *Session) maybeIPv6Up() {
	if s.phase != phaseIPv6CP || !s.ipv6LocalOpen || !s.ipv6RemoteOpen {
		return
	}
	s.bringUpLocked()
}

// ipv6GiveUpLocked abandons IPv6CP once its request budget is spent: on a
// reliable transport a peer that never answered the first request does not
// implement the protocol (SoftEther's case), so the link comes up IPv4-only
// unless strict — bringUpLocked owns that verdict.
func (s *Session) ipv6GiveUpLocked() {
	if s.phase != phaseIPv6CP {
		return
	}
	s.ipv6Restart.stop()
	s.bringUpLocked()
}
