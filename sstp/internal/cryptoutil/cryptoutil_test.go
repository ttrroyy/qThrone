package cryptoutil

import (
	"bytes"
	"crypto/ecdh"
	"crypto/sha256"
	"testing"
)

func TestDHAgreement(t *testing.T) {
	groups := []struct {
		name string
		mk   func() DHGroup
	}{
		{"Curve25519", func() DHGroup { return NewECDH(ecdh.X25519(), false) }},
		{"ECP-256", func() DHGroup { return NewECDH(ecdh.P256(), true) }},
		{"ECP-384", func() DHGroup { return NewECDH(ecdh.P384(), true) }},
		{"ECP-521", func() DHGroup { return NewECDH(ecdh.P521(), true) }},
		{"MODP-2048", NewMODP2048},
	}
	for _, g := range groups {
		t.Run(g.name, func(t *testing.T) {
			a, b := g.mk(), g.mk()
			apub, err := a.Generate()
			if err != nil {
				t.Fatalf("a.Generate: %v", err)
			}
			bpub, err := b.Generate()
			if err != nil {
				t.Fatalf("b.Generate: %v", err)
			}
			as, err := a.ComputeSecret(bpub)
			if err != nil {
				t.Fatalf("a.ComputeSecret: %v", err)
			}
			bs, err := b.ComputeSecret(apub)
			if err != nil {
				t.Fatalf("b.ComputeSecret: %v", err)
			}
			if !bytes.Equal(as, bs) {
				t.Fatal("shared secrets differ")
			}
		})
	}
}

// TestECDHPointPrefix pins the wire difference between the NIST curves, which
// transmit bare X||Y (RFC 5903), and X25519, which has no point prefix.
func TestECDHPointPrefix(t *testing.T) {
	stripped := NewECDH(ecdh.P256(), true)
	pub, err := stripped.Generate()
	if err != nil {
		t.Fatal(err)
	}
	// An uncompressed P-256 point is 1+32+32; stripped it is 64.
	if len(pub) != 64 {
		t.Fatalf("P-256 public value = %d bytes, want 64 (prefix stripped)", len(pub))
	}

	kept := NewECDH(ecdh.P256(), false)
	pub2, err := kept.Generate()
	if err != nil {
		t.Fatal(err)
	}
	if len(pub2) != 65 || pub2[0] != 0x04 {
		t.Fatalf("unstripped P-256 public value = %d bytes (first %#x), want 65 starting 0x04",
			len(pub2), pub2[0])
	}
}

// TestPRFPlus checks prf+ output length and that T1 = prf(K, S|0x01).
func TestPRFPlus(t *testing.T) {
	prf := NewHMACPRF(sha256.New)
	key := []byte("key")
	seed := []byte("seed")
	out := prf.Plus(key, seed, 100)
	if len(out) != 100 {
		t.Fatalf("prf+ length = %d, want 100", len(out))
	}
	t1 := prf.Apply(key, append(append([]byte(nil), seed...), 0x01))
	if !bytes.Equal(out[:prf.Size], t1) {
		t.Fatal("prf+ T1 mismatch")
	}
}

func TestGCMRoundTrip(t *testing.T) {
	c, err := NewAESGCMSKCipher(256)
	if err != nil {
		t.Fatal(err)
	}
	key := make([]byte, c.KeyLen())
	for i := range key {
		key[i] = byte(i)
	}
	aad := []byte("ike-header-and-sk-header")
	pt := []byte("the quick brown fox jumps over the lazy dog")
	sealed, err := c.Seal(key, nil, aad, pt)
	if err != nil {
		t.Fatal(err)
	}
	got, err := c.Open(key, nil, aad, sealed)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, pt) {
		t.Fatalf("gcm round trip: got %q", got)
	}
	// Tamper with AAD -> must fail.
	if _, err := c.Open(key, nil, []byte("wrong-aad"), sealed); err == nil {
		t.Fatal("gcm accepted tampered AAD")
	}
}

func TestCBCETMRoundTrip(t *testing.T) {
	c, err := NewAESCBCSKCipher(256)
	if err != nil {
		t.Fatal(err)
	}
	cbc := c.(*cbcCipher)
	integ := NewHMACIntegrity(sha256.New, 32, 16)
	encKey := bytes.Repeat([]byte{0xab}, cbc.KeyLen())
	integKey := bytes.Repeat([]byte{0xcd}, integ.KeyLen)
	aad := []byte("header")
	// SealETM expects block-aligned input; pad to 48 bytes (3x AES block).
	pt := make([]byte, 48)
	copy(pt, []byte("encrypt-then-mac payload contents"))
	sealed, err := cbc.SealETM(encKey, integKey, aad, pt, integ)
	if err != nil {
		t.Fatal(err)
	}
	got, err := cbc.OpenETM(encKey, integKey, aad, sealed, integ)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, pt) {
		t.Fatalf("cbc round trip: got %q", got)
	}
	// Flip a ciphertext bit -> MAC must reject.
	sealed[len(sealed)-integ.ICVLen-1] ^= 0x01
	if _, err := cbc.OpenETM(encKey, integKey, aad, sealed, integ); err == nil {
		t.Fatal("cbc accepted tampered ciphertext")
	}
}

func TestAESKeyLenRejectsBadLengths(t *testing.T) {
	if _, err := NewAESGCMSKCipher(123); err == nil {
		t.Fatal("accepted a 123-bit AES key")
	}
	// 0 selects the 256-bit default.
	c, err := NewAESGCMSKCipher(0)
	if err != nil {
		t.Fatal(err)
	}
	if got := c.KeyLen(); got != 32+4 { // + GCM salt
		t.Fatalf("default GCM KeyLen = %d, want 36", got)
	}
}
