package tunnel

import (
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"math/big"
	"net"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"
)

// testCert is a throwaway self-signed certificate for the local TLS server.
func testCert(t *testing.T) tls.Certificate {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatalf("generate key: %v", err)
	}
	tmpl := x509.Certificate{
		SerialNumber:          big.NewInt(1),
		Subject:               pkix.Name{CommonName: "localhost"},
		NotBefore:             time.Now().Add(-time.Hour),
		NotAfter:              time.Now().Add(time.Hour),
		KeyUsage:              x509.KeyUsageDigitalSignature | x509.KeyUsageCertSign,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		BasicConstraintsValid: true,
	}
	der, err := x509.CreateCertificate(rand.Reader, &tmpl, &tmpl, &key.PublicKey, key)
	if err != nil {
		t.Fatalf("self-sign: %v", err)
	}
	return tls.Certificate{Certificate: [][]byte{der}, PrivateKey: key}
}

// TestDialConnIsUsedAndWrappedInTLS covers the additive Config.DialConn
// path: Dial must dial through the caller's function and then wrap the raw
// connection with tls.Client using this config's tlsConfig, so the SNI and
// minimum version still reach the server.
func TestDialConnIsUsedAndWrappedInTLS(t *testing.T) {
	cert := testCert(t)

	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	defer ln.Close()

	var (
		mu        sync.Mutex
		serverSNI string
		serverVer uint16
		handshook = make(chan struct{}, 1)
	)
	go func() {
		conn, err := ln.Accept()
		if err != nil {
			return
		}
		tc := tls.Server(conn, &tls.Config{Certificates: []tls.Certificate{cert}})
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		if err := tc.HandshakeContext(ctx); err == nil {
			st := tc.ConnectionState()
			mu.Lock()
			serverSNI = st.ServerName
			serverVer = st.Version
			mu.Unlock()
			handshook <- struct{}{}
		}
		_ = tc.Close()
	}()

	host, portStr, err := net.SplitHostPort(ln.Addr().String())
	if err != nil {
		t.Fatalf("split: %v", err)
	}
	port, err := strconv.Atoi(portStr)
	if err != nil {
		t.Fatalf("port: %v", err)
	}

	var (
		dialMu   sync.Mutex
		dialedOK bool
		dialAddr string
	)
	cfg := Config{
		Server:      host,
		Port:        port,
		SNI:         "sni.example.test",
		Username:    "user",
		Password:    "pass",
		TLSMode:     TLSInsecure,
		DialTimeout: 5 * time.Second,
		DialConn: func(network, addr string) (net.Conn, error) {
			dialMu.Lock()
			dialAddr = addr
			dialMu.Unlock()
			c, err := (&net.Dialer{}).Dial(network, addr)
			if err == nil {
				dialMu.Lock()
				dialedOK = true
				dialMu.Unlock()
			}
			return c, err
		},
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	// The dummy server never speaks SSTP, so establishment must fail — the
	// assertions below are about the transport, not the outcome.
	sess, _, err := Dial(ctx, cfg)
	if sess != nil {
		sess.Close()
	}
	if err == nil {
		t.Fatal("expected establishment to fail against a non-SSTP server")
	}
	if strings.Contains(err.Error(), "missing address") || strings.Contains(err.Error(), "connect: connection refused") {
		t.Fatalf("DialConn bypassed or refused: %v", err)
	}

	dialMu.Lock()
	ok, addr := dialedOK, dialAddr
	dialMu.Unlock()
	if !ok {
		t.Fatal("DialConn was never invoked")
	}
	if want := net.JoinHostPort(host, portStr); addr != want {
		t.Errorf("DialConn addr = %q, want %q", addr, want)
	}

	select {
	case <-handshook:
	case <-time.After(5 * time.Second):
		t.Fatal("server never completed a TLS handshake: the raw conn was not wrapped")
	}

	mu.Lock()
	defer mu.Unlock()
	if serverSNI != "sni.example.test" {
		t.Errorf("SNI = %q, want %q", serverSNI, "sni.example.test")
	}
	if serverVer < tls.VersionTLS12 {
		t.Errorf("negotiated version %#x below TLS 1.2", serverVer)
	}
}
