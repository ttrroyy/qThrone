// Package tunnel is the SwiftSSTP SSTP client engine: TLS + MS-SSTP + PPP
// (MS-CHAPv2) with a packet-level data path. Unlike a host-VPN client it owns
// no interface: Dial returns raw inner IP packets through ReadIP/WriteIP, so
// the same engine feeds a VpnService TUN (Android), a Wintun adapter
// (Windows), a gVisor netstack (proxy mode), or a test harness.
//
// The protocol stack (PPP/MS-CHAPv2/MPPE, SSTP wire codec, crypto binding) is
// vendored from the MIT-licensed veepin project; see NOTICE.
package tunnel

import (
	"context"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"encoding/hex"
	"errors"
	"fmt"
	"log/slog"
	"net"
	"strconv"
	"sync"
	"sync/atomic"
	"time"

	"swiftsstp/internal/mschap"
	ppp "swiftsstp/internal/ppp"
	"swiftsstp/internal/sstp"
	"swiftsstp/internal/sstp/wire"
	"swiftsstp/internal/vlog"
)

// TLSMode selects how the server certificate is treated. SSTP's inner auth
// (MS-CHAPv2) mutually authenticates the peers regardless: a server that
// cannot prove knowledge of the password cannot complete the handshake even
// with verification off. The certificate layer protects metadata (username
// travels inside the TLS channel but MS-CHAPv2 is challenge-response).
type TLSMode int

const (
	// TLSVerify performs standard chain + hostname verification (default).
	TLSVerify TLSMode = iota
	// TLSInsecure accepts any certificate — self-signed servers, no CA
	// management; suitable for servers with self-signed certificates.
	TLSInsecure
	// TLSCustomCA verifies against exactly the PEM certificates in
	// Config.CACerts (a pinned private CA), ignoring the system store.
	TLSCustomCA
)

// DefaultMTU is the inner MTU when Config.MTU is 0. 1400 leaves room for
// MPPE and PPP framing inside a TLS record on a typical path.
const DefaultMTU = 1400

// Config parameters for one SSTP tunnel. Only Server/Username/Password are
// required.
type Config struct {
	Server   string // host or IP (no port)
	Port     int    // 0 = 443
	SNI      string // TLS Server Name Indication; empty = Server (even for IPs)
	Username string
	Password string

	TLSMode TLSMode
	CACerts []byte // PEM bundle, required for TLSCustomCA

	MTU int // inner MTU; 0 = DefaultMTU

	// EnableIPv6 negotiates IPv6CP (RFC 5072) alongside IPCP. Servers that do
	// not implement IPv6CP (SoftEther, most SSTP deployments) are not fatal:
	// the tunnel comes up IPv4-only with Result.IPv6Negotiated false, unless
	// IPv6Strict is also set.
	EnableIPv6 bool
	// IPv6Strict fails the dial when IPv6CP does not complete, instead of
	// degrading to IPv4-only. Meaningful only with EnableIPv6.
	IPv6Strict bool

	DialTimeout time.Duration // 0 = 20s for the TCP+TLS+PPP establishment
	KeepAlive   time.Duration // LCP echo interval; 0 = 30s, negative disables

	// DialConn, when non-nil, supplies the raw TCP connection instead of the
	// internal tls.Dialer. Dial dials via this function (typically a plain
	// net.Dialer, possibly with the socket already protected from routing by
	// a VPN) and then wraps the returned conn with tls.Client using this
	// config's tlsConfig, so SNI, MinVersion and verification mode are
	// unchanged. The conn must already be connected to Server:Port.
	DialConn func(network, addr string) (net.Conn, error)

	// Proxy routes the outer transport through an upstream HTTP CONNECT or
	// SOCKS5 proxy. Ignored when DialConn is
	// set (DialConn wins — it already owns connection establishment).
	Proxy *ProxyConfig

	Logger *slog.Logger
}

func (c *Config) validate() error {
	if c.Server == "" {
		return errors.New("tunnel: server is required")
	}
	if c.Username == "" || c.Password == "" {
		return errors.New("tunnel: username and password are required")
	}
	if c.TLSMode == TLSCustomCA && len(c.CACerts) == 0 {
		return errors.New("tunnel: TLSCustomCA requires CACerts")
	}
	return nil
}

// tlsConfig builds the crypto/tls configuration for this tunnel.
func (c *Config) tlsConfig() (*tls.Config, error) {
	sni := c.SNI
	if sni == "" {
		sni = c.Server
	}
	tc := &tls.Config{ServerName: sni, MinVersion: tls.VersionTLS12}
	switch c.TLSMode {
	case TLSInsecure:
		tc.InsecureSkipVerify = true //nolint:gosec // explicit user opt-in
	case TLSCustomCA:
		pool := x509.NewCertPool()
		if !pool.AppendCertsFromPEM(c.CACerts) {
			return nil, errors.New("tunnel: no certificates parsed from CACerts")
		}
		tc.RootCAs = pool
		// A pinned private CA often issues for an internal name; hostname
		// checks still apply against SNI, which is the point of the SNI knob.
	}
	return tc, nil
}

// State is the lifecycle state reported to OnState.
type State int

const (
	StateConnecting State = iota
	StateConnected
	StateClosed
)

func (s State) String() string {
	switch s {
	case StateConnecting:
		return "connecting"
	case StateConnected:
		return "connected"
	case StateClosed:
		return "closed"
	}
	return "unknown"
}

// Result is what the server assigned, for the caller to apply wherever it
// terminates the inner packets (VpnService addresses/DNS, Wintun config,
// netstack stack config).
type Result struct {
	LocalIP net.IP // our inner address
	PeerIP  net.IP // server inner address (point-to-point peer)
	DNS     []net.IP
	MTU     int

	// LocalIP6/PeerIP6 are the IPv6CP-negotiated interface identifiers (RFC
	// 5072), not full addresses: each side forms its own address from its IID,
	// typically the link-local fe80::/64 plus LocalIP6. Both are nil unless
	// IPv6Negotiated is true.
	LocalIP6 net.IP
	PeerIP6  net.IP
	// IPv6Negotiated reports whether IPv6CP completed. A server without IPv6CP
	// support leaves it false and the tunnel carries IPv4 — unless
	// Config.IPv6Strict, which fails the dial instead of degrading.
	IPv6Negotiated bool

	ServerCertSHA256 string // fingerprint of the presented cert, for pinning UIs
}

// Session is a live SSTP tunnel. Safe for concurrent use. One goroutine reads
// the TLS connection; ReadIP consumes the inbound packet queue; WriteIP is
// safe from any goroutine.
type Session struct {
	cfg     Config
	logger  *vlog.Logger
	tlsConn *tls.Conn
	ppp     *ppp.Session

	result Result

	inbound     chan []byte // inbound IP packets, readLoop -> ReadIP
	done        chan struct{}
	ipReady     chan struct{} // closed when IPCP is up
	serverNonce []byte        // server crypto-binding nonce from CallConnectAck
	closeOnce   sync.Once
	closeErr    error
	mu          sync.Mutex // guards closeErr

	writeMu sync.Mutex // one concurrent TLS writer (ReadIP side control + WriteIP)

	up   atomic.Uint64 // inner bytes written to the tunnel
	down atomic.Uint64 // inner bytes read from the tunnel

	onState func(State, error)
}

const inboundQueue = 512

// NewStubSession returns a Session that owns no transport: Counters read zero,
// Wait blocks until Close, and ReadIP/WriteIP fail once it is closed. It gives
// code that consumes a *Session — reconnect managers, UI presenters, tests — a
// real session value to hold without a server behind one.
func NewStubSession() *Session {
	return &Session{
		inbound: make(chan []byte, inboundQueue),
		done:    make(chan struct{}),
		ipReady: make(chan struct{}),
	}
}

// Dial establishes TCP+TLS, performs the MS-SSTP handshake and PPP
// negotiation (LCP, MS-CHAPv2, crypto binding, IPCP), and returns a live
// session plus the negotiated Result. On auth rejection the error satisfies
// IsAuthError.
func Dial(ctx context.Context, cfg Config) (*Session, Result, error) {
	var zero Result
	if err := cfg.validate(); err != nil {
		return nil, zero, err
	}
	logger := vlog.From(cfg.Logger)
	port := cfg.Port
	if port == 0 {
		port = 443
	}
	addr := net.JoinHostPort(cfg.Server, strconv.Itoa(port))

	tlsCfg, err := cfg.tlsConfig()
	if err != nil {
		return nil, zero, err
	}

	var tlsConn *tls.Conn
	switch {
	case cfg.DialConn != nil:
		raw, err := cfg.DialConn("tcp", addr)
		if err != nil {
			return nil, zero, fmt.Errorf("tunnel: dial %s: %w", addr, err)
		}
		tc := tls.Client(raw, tlsCfg)
		if err := tc.HandshakeContext(ctx); err != nil {
			_ = raw.Close()
			return nil, zero, fmt.Errorf("tunnel: tls handshake: %w", err)
		}
		tlsConn = tc
	case cfg.Proxy != nil:
		raw, err := cfg.Proxy.proxyDial(ctx, addr)
		if err != nil {
			return nil, zero, err
		}
		tc := tls.Client(raw, tlsCfg)
		if err := tc.HandshakeContext(ctx); err != nil {
			_ = raw.Close()
			return nil, zero, fmt.Errorf("tunnel: tls handshake via proxy: %w", err)
		}
		tlsConn = tc
	default:
		d := tls.Dialer{Config: tlsCfg}
		conn, err := d.DialContext(ctx, "tcp", addr)
		if err != nil {
			return nil, zero, fmt.Errorf("tunnel: dial %s: %w", addr, err)
		}
		tlsConn = conn.(*tls.Conn)
	}

	var certFP string
	if pcs := tlsConn.ConnectionState().PeerCertificates; len(pcs) > 0 {
		sum := sha256.Sum256(pcs[0].Raw)
		certFP = hex.EncodeToString(sum[:])
	}

	if err := sstp.HTTPHandshake(tlsConn, cfg.Server); err != nil {
		tlsConn.Close()
		return nil, zero, fmt.Errorf("tunnel: HTTP handshake: %w", err)
	}
	if err := sstp.SendCallConnectRequest(tlsConn); err != nil {
		tlsConn.Close()
		return nil, zero, fmt.Errorf("tunnel: CallConnectRequest: %w", err)
	}
	serverNonce, err := sstp.ReadCallConnectAck(tlsConn)
	if err != nil {
		tlsConn.Close()
		return nil, zero, fmt.Errorf("tunnel: CallConnectAck: %w", err)
	}

	s := &Session{
		cfg:         cfg,
		logger:      logger,
		tlsConn:     tlsConn,
		inbound:     make(chan []byte, inboundQueue),
		done:        make(chan struct{}),
		ipReady:     make(chan struct{}),
		serverNonce: serverNonce,
		result:      Result{MTU: effectiveMTU(cfg.MTU), ServerCertSHA256: certFP},
	}
	s.ppp = ppp.New(cfg.Username, cfg.Password, s, s)
	if cfg.MTU != 0 {
		s.ppp.SetMRU(cfg.MTU)
	}
	s.ppp.SetIPv6(cfg.EnableIPv6)
	s.ppp.SetIPv6Strict(cfg.IPv6Strict)

	// Establish within the dial timeout (or caller ctx): wait for IPCP up.
	estCtx := ctx
	if cfg.DialTimeout > 0 {
		var cancel context.CancelFunc
		estCtx, cancel = context.WithTimeout(ctx, cfg.DialTimeout)
		defer cancel()
	}

	s.ppp.Start()
	go s.readLoop()

	select {
	case <-s.ipReady:
	case <-estCtx.Done():
		s.Close()
		return nil, zero, fmt.Errorf("tunnel: establish: %w (%v)", s.establishError(), estCtx.Err())
	case <-s.done:
		s.Close()
		return nil, zero, s.establishError()
	}

	s.startKeepAlive()
	logger.Printf("tunnel: up, local %s peer %s dns %v mtu %d ipv6 %v",
		s.result.LocalIP, s.result.PeerIP, s.result.DNS, s.result.MTU, s.result.IPv6Negotiated)
	return s, s.result, nil
}

// effectiveMTU clamps a user MTU or falls back to the default.
func effectiveMTU(mtu int) int {
	if mtu == 0 {
		return DefaultMTU
	}
	if mtu < 576 {
		return 576
	}
	if mtu > 1500 {
		return 1500
	}
	return mtu
}

// IsAuthError reports whether err is a rejected credential (as opposed to a
// transport/protocol failure), so UIs can prompt for a new password instead
// of retrying blindly.
func IsAuthError(err error) bool { return errors.Is(err, ppp.ErrAuth) }

// ---- session plumbing ----

func (s *Session) OnState(fn func(State, error)) { s.onState = fn }

func (s *Session) establishError() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.closeErr != nil {
		return s.closeErr
	}
	return errors.New("tunnel: establishment aborted")
}

// Result returns the negotiated configuration.
func (s *Session) Result() Result { return s.result }

// ReadIP blocks until the next inner IP packet arrives, copying it into buf
// and returning its length, or an error once the session is closed. buf must
// be at least 65535 bytes.
func (s *Session) ReadIP(buf []byte) (int, error) {
	select {
	case pkt, ok := <-s.inbound:
		if !ok {
			return 0, s.err()
		}
		n := copy(buf, pkt)
		return n, nil
	case <-s.done:
		// drain what's queued before reporting closure
		select {
		case pkt := <-s.inbound:
			return copy(buf, pkt), nil
		default:
			return 0, s.err()
		}
	}
}

// WriteIP sends one inner IP packet into the tunnel.
func (s *Session) WriteIP(pkt []byte) error {
	select {
	case <-s.done:
		return s.err()
	default:
	}
	frame := ppp.EncapsulateIP(pkt)
	wirePkt, err := wire.EncodeData(frame)
	if err != nil {
		return err
	}
	if err := s.writePacket(wirePkt); err != nil {
		s.Close()
		return err
	}
	s.up.Add(uint64(len(pkt)))
	return nil
}

// Counters returns cumulative inner bytes since the session came up.
func (s *Session) Counters() (up, down uint64) { return s.up.Load(), s.down.Load() }

// Wait blocks until the session ends and returns the close cause.
func (s *Session) Wait() error {
	<-s.done
	return s.err()
}

// Close tears the session down; subsequent Wait returns the close cause.
func (s *Session) Close() error {
	s.closeOnce.Do(func() {
		if s.tlsConn != nil {
			// Bound disconnect even when another writer is blocked on a dead peer.
			_ = s.tlsConn.SetWriteDeadline(time.Now().Add(500 * time.Millisecond))
			statusVal := []byte{0x00, 0x00, 0x00, 0x00}
			attrs := []wire.Attribute{{ID: wire.AttrStatusInfo, Value: statusVal}}
			if pkt, err := wire.EncodeControl(wire.MsgCallDisconnect, attrs); err == nil {
				_ = s.writePacket(pkt)
			}
		}
		close(s.done)
		if s.tlsConn != nil {
			_ = s.tlsConn.Close()
		}
		if s.onState != nil {
			s.onState(StateClosed, s.err())
		}
	})
	return s.err()
}

func (s *Session) err() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.closeErr != nil {
		return s.closeErr
	}
	return errors.New("tunnel: closed")
}

func (s *Session) fail(err error) {
	s.mu.Lock()
	if s.closeErr == nil {
		s.closeErr = err
	}
	s.mu.Unlock()
	_ = s.Close()
}

func (s *Session) writePacket(pkt []byte) error {
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	if s.tlsConn == nil {
		// A session that owns no connection (NewStubSession) has nothing to
		// write to; report it rather than panicking on the nil conn.
		return errors.New("tunnel: not connected")
	}
	_ = s.tlsConn.SetWriteDeadline(time.Now().Add(30 * time.Second))
	_, err := s.tlsConn.Write(pkt)
	return err
}

// ppp.Transport — one framed PPP packet into an SSTP data packet.
func (s *Session) SendPPP(frame []byte) error {
	pkt, err := wire.EncodeData(frame)
	if err != nil {
		return err
	}
	return s.writePacket(pkt)
}

// ppp.Handler — auth success (crypto binding) and IPCP up.
func (s *Session) Authenticated(ntResponse [mschap.NTResponseLen]byte) {
	hlak := mschap.ClientHLAK(s.cfg.Password, ntResponse)
	state := s.tlsConn.ConnectionState()
	if len(state.PeerCertificates) == 0 {
		s.logger.Printf("tunnel: no server certificate for crypto binding")
		return
	}
	pkt, err := sstp.BuildCallConnected(s.serverNonce, state.PeerCertificates[0].Raw, hlak)
	if err != nil {
		s.fail(fmt.Errorf("tunnel: build CallConnected: %w", err))
		return
	}
	if err := s.writePacket(pkt); err != nil {
		s.fail(fmt.Errorf("tunnel: send CallConnected: %w", err))
		return
	}
	s.logger.Printf("tunnel: crypto binding sent")
}

func (s *Session) NetworkUp(cfg ppp.IPConfig) {
	s.result.LocalIP = cfg.LocalIP
	s.result.PeerIP = cfg.PeerIP
	s.result.DNS = cfg.DNS
	s.result.LocalIP6 = cfg.LocalIP6
	s.result.PeerIP6 = cfg.PeerIP6
	s.result.IPv6Negotiated = cfg.LocalIP6 != nil && cfg.PeerIP6 != nil
	close(s.ipReady)
	if s.onState != nil {
		s.onState(StateConnected, nil)
	}
}

func (s *Session) Closed(err error) { s.fail(err) }

func (s *Session) readLoop() {
	for {
		control, body, err := wire.ReadPacket(s.tlsConn)
		if err != nil {
			s.fail(fmt.Errorf("tunnel: read: %w", err))
			return
		}
		if control {
			msg, err := wire.ParseControl(body)
			if err != nil {
				s.logger.Printf("tunnel: malformed control: %v", err)
				continue
			}
			switch msg.Type {
			case wire.MsgCallDisconnect:
				s.fail(errors.New("tunnel: server disconnected"))
				return
			case wire.MsgEchoRequest:
				resp, _ := wire.EncodeControl(wire.MsgEchoResponse, nil)
				_ = s.writePacket(resp)
			case wire.MsgCallConnected:
				s.logger.Printf("tunnel: server crypto binding ack")
			default:
				s.logger.Printf("tunnel: unhandled control %#x", msg.Type)
			}
			continue
		}
		if ipPacket, ok := ppp.IsIP(body); ok {
			s.down.Add(uint64(len(ipPacket)))
			select {
			case s.inbound <- ipPacket:
			case <-s.done:
				return
			default:
				// Queue full: drop. A consumer that stopped reading has a
				// bigger problem, and blocking the TLS reader would stall
				// PPP control too.
				s.logger.Printf("tunnel: inbound queue full, dropping packet")
			}
			continue
		}
		s.ppp.Receive(body)
	}
}

// startKeepAlive sends LCP Echo-Requests periodically; a dead peer surfaces
// as a read/write error instead of a silently black-holed tunnel.
func (s *Session) startKeepAlive() {
	iv := s.cfg.KeepAlive
	if iv == 0 {
		iv = 30 * time.Second
	}
	if iv < 0 {
		return
	}
	go func() {
		t := time.NewTicker(iv)
		defer t.Stop()
		for {
			select {
			case <-s.done:
				return
			case <-t.C:
				ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
				if err := s.ppp.SendEcho(ctx); err != nil {
					s.logger.Printf("tunnel: keepalive: %v", err)
				}
				cancel()
			}
		}
	}()
}
