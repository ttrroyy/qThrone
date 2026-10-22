package cryptoutil

import (
	"bytes"
	"encoding/hex"
	"testing"
)

func mustHex(t *testing.T, s string) []byte {
	t.Helper()
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatalf("bad hex %q: %v", s, err)
	}
	return b
}

// TestChaCha20Poly1305RFC8439 checks the AEAD against the worked example in
// RFC 8439 section 2.8.2, so a WireGuard handshake failure can never be blamed
// on the cipher.
func TestChaCha20Poly1305RFC8439(t *testing.T) {
	key := mustHex(t, "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f")
	nonce := mustHex(t, "070000004041424344454647")
	aad := mustHex(t, "50515253c0c1c2c3c4c5c6c7")
	plaintext := []byte("Ladies and Gentlemen of the class of '99: If I could offer you " +
		"only one tip for the future, sunscreen would be it.")

	// Ciphertext || tag, from RFC 8439 §2.8.2.
	want := mustHex(t,
		"d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"+
			"3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"+
			"92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"+
			"3ff4def08e4b7a9de576d26586cec64b6116"+
			"1ae10b594f09e26a7e902ecbd0600691")

	aead, err := NewChaCha20Poly1305(key)
	if err != nil {
		t.Fatal(err)
	}
	got := aead.Seal(nil, nonce, plaintext, aad)
	if !bytes.Equal(got, want) {
		t.Fatalf("Seal mismatch:\n got %x\nwant %x", got, want)
	}

	back, err := aead.Open(nil, nonce, got, aad)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(back, plaintext) {
		t.Fatalf("Open round trip: got %q", back)
	}

	// A tampered tag must be rejected.
	bad := append([]byte(nil), got...)
	bad[len(bad)-1] ^= 0x01
	if _, err := aead.Open(nil, nonce, bad, aad); err == nil {
		t.Fatal("accepted a tampered tag")
	}
	// So must a wrong AAD.
	if _, err := aead.Open(nil, nonce, got, []byte("wrong")); err == nil {
		t.Fatal("accepted wrong AAD")
	}
}

// TestBLAKE2sRFC7693 checks unkeyed BLAKE2s-256 against the RFC 7693 appendix B
// example ("abc").
func TestBLAKE2sRFC7693(t *testing.T) {
	h := NewBLAKE2s()
	h.Write([]byte("abc"))
	got := h.Sum(nil)
	want := mustHex(t, "508c5e8c327c14e2e1a72ba34eeb452f37458b209ed63a294d999b4c86675982")
	if !bytes.Equal(got, want) {
		t.Fatalf("BLAKE2s(\"abc\") = %x, want %x", got, want)
	}
	if h.Size() != BLAKE2sSize || BLAKE2sSize != 32 {
		t.Fatalf("BLAKE2s size = %d, want 32", h.Size())
	}
}

// TestBLAKE2sMACIsKeyed pins BLAKE2's native keyed mode: the digest must depend
// on the key, and must differ from the unkeyed hash of the same input.
func TestBLAKE2sMACIsKeyed(t *testing.T) {
	msg := []byte("wireguard mac1 input")
	keyA := bytes.Repeat([]byte{0xa1}, 32)
	keyB := bytes.Repeat([]byte{0xb2}, 32)

	mac := func(key []byte) []byte {
		h, err := NewBLAKE2s256MAC(key)
		if err != nil {
			t.Fatal(err)
		}
		h.Write(msg)
		return h.Sum(nil)
	}

	a, b := mac(keyA), mac(keyB)
	if bytes.Equal(a, b) {
		t.Fatal("keyed BLAKE2s ignored the key")
	}
	unkeyed := NewBLAKE2s()
	unkeyed.Write(msg)
	if bytes.Equal(a, unkeyed.Sum(nil)) {
		t.Fatal("keyed BLAKE2s matched the unkeyed hash")
	}
	// Deterministic for a given key.
	if !bytes.Equal(a, mac(keyA)) {
		t.Fatal("keyed BLAKE2s is not deterministic")
	}
}

// TestXChaCha20Poly1305NonceSize guards the variant WireGuard cookie replies
// need: a 24-octet nonce, distinct from the 12-octet transport AEAD.
func TestXChaCha20Poly1305NonceSize(t *testing.T) {
	key := bytes.Repeat([]byte{0x42}, ChaCha20Poly1305KeySize)
	x, err := NewXChaCha20Poly1305(key)
	if err != nil {
		t.Fatal(err)
	}
	if x.NonceSize() != 24 {
		t.Fatalf("XChaCha20 nonce = %d, want 24", x.NonceSize())
	}
	plain, err := NewChaCha20Poly1305(key)
	if err != nil {
		t.Fatal(err)
	}
	if plain.NonceSize() != 12 {
		t.Fatalf("ChaCha20 nonce = %d, want 12", plain.NonceSize())
	}

	nonce := bytes.Repeat([]byte{0x07}, x.NonceSize())
	ct := x.Seal(nil, nonce, []byte("cookie"), nil)
	back, err := x.Open(nil, nonce, ct, nil)
	if err != nil || string(back) != "cookie" {
		t.Fatalf("XChaCha round trip: %q, %v", back, err)
	}
}

// TestBLAKE2s128MACWidth pins the digest width WireGuard's MAC() actually is.
// mac1, mac2 and the cookie are all 128 bits; using the 256-bit keyed hash would
// still be "a keyed BLAKE2s" and would still interop with itself, while every
// real peer rejected it.
func TestBLAKE2s128MACWidth(t *testing.T) {
	key := bytes.Repeat([]byte{0x5a}, 32)
	h, err := NewBLAKE2s128MAC(key)
	if err != nil {
		t.Fatal(err)
	}
	h.Write([]byte("handshake initiation up to mac1"))
	sum := h.Sum(nil)
	if len(sum) != BLAKE2s128Size || BLAKE2s128Size != 16 {
		t.Fatalf("BLAKE2s-128 digest = %d octets, want 16", len(sum))
	}

	// It must be a different function from the 256-bit one, not a truncation.
	h256, err := NewBLAKE2s256MAC(key)
	if err != nil {
		t.Fatal(err)
	}
	h256.Write([]byte("handshake initiation up to mac1"))
	if bytes.Equal(sum, h256.Sum(nil)[:16]) {
		t.Fatal("BLAKE2s-128 equals the truncated 256-bit digest; " +
			"the digest length must be bound into the parameter block")
	}
}
