package cryptoutil

import (
	"crypto/ecdh"
	"crypto/rand"
	"crypto/sha1"
	"crypto/sha256"
	"crypto/sha512"
	"hash"
	"testing"
)

// dhGroups are the groups the IKE layer can negotiate, named as they appear in
// the IANA registry so benchmark output stays comparable across the rename.
var dhGroups = []struct {
	name string
	mk   func() DHGroup
}{
	{"Curve25519", func() DHGroup { return NewECDH(ecdh.X25519(), false) }},
	{"ECP-256", func() DHGroup { return NewECDH(ecdh.P256(), true) }},
	{"ECP-384", func() DHGroup { return NewECDH(ecdh.P384(), true) }},
	{"MODP-2048", NewMODP2048},
}

// BenchmarkDHGenerate measures ephemeral key generation per group — this runs
// once per handshake on each side.
func BenchmarkDHGenerate(b *testing.B) {
	for _, g := range dhGroups {
		b.Run(g.name, func(b *testing.B) {
			b.ReportAllocs()
			for i := 0; i < b.N; i++ {
				if _, err := g.mk().Generate(); err != nil {
					b.Fatal(err)
				}
			}
		})
	}
}

// BenchmarkDHComputeSecret measures the shared-secret computation — the
// dominant asymmetric cost of the handshake.
func BenchmarkDHComputeSecret(b *testing.B) {
	for _, g := range dhGroups {
		b.Run(g.name, func(b *testing.B) {
			a, peer := g.mk(), g.mk()
			_, _ = a.Generate()
			peerPub, _ := peer.Generate()
			b.ReportAllocs()
			b.ResetTimer()
			for i := 0; i < b.N; i++ {
				if _, err := a.ComputeSecret(peerPub); err != nil {
					b.Fatal(err)
				}
			}
		})
	}
}

// BenchmarkPRFPlus measures prf+ expansion at a representative output length.
func BenchmarkPRFPlus(b *testing.B) {
	for _, tc := range []struct {
		name string
		h    func() hash.Hash
	}{
		{"SHA1", sha1.New},
		{"SHA256", sha256.New},
		{"SHA512", sha512.New},
	} {
		b.Run(tc.name, func(b *testing.B) {
			prf := NewHMACPRF(tc.h)
			key := randBytes(prf.Size)
			seed := randBytes(80)
			b.ReportAllocs()
			for i := 0; i < b.N; i++ {
				prf.Plus(key, seed, 128)
			}
		})
	}
}

// BenchmarkCipherSeal measures raw SK-cipher seal throughput on a handshake-
// sized payload (distinct from the ESP data-plane benchmarks).
func BenchmarkCipherSeal(b *testing.B) {
	ciphers := []struct {
		name    string
		keyBits int
	}{
		{"AES128-GCM", 128},
		{"AES256-GCM", 256},
	}
	pt := randBytes(256)
	aad := randBytes(32)
	for _, c := range ciphers {
		b.Run(c.name, func(b *testing.B) {
			sk, err := NewAESGCMSKCipher(c.keyBits)
			if err != nil {
				b.Fatal(err)
			}
			key := randBytes(sk.KeyLen())
			b.SetBytes(int64(len(pt)))
			b.ReportAllocs()
			b.ResetTimer()
			for i := 0; i < b.N; i++ {
				if _, err := sk.Seal(key, nil, aad, pt); err != nil {
					b.Fatal(err)
				}
			}
		})
	}
}

func randBytes(n int) []byte {
	b := make([]byte, n)
	_, _ = rand.Read(b)
	return b
}

// BenchmarkAEADSeal compares the data-path AEADs at a typical MTU-sized packet.
// WireGuard fixes ChaCha20-Poly1305; IKEv2 negotiates AES-GCM. Both ride the
// same pump, so their relative cost is worth knowing.
func BenchmarkAEADSeal(b *testing.B) {
	pt := randBytes(1400)
	aad := randBytes(16)
	key := randBytes(ChaCha20Poly1305KeySize)

	b.Run("ChaCha20-Poly1305", func(b *testing.B) {
		aead, err := NewChaCha20Poly1305(key)
		if err != nil {
			b.Fatal(err)
		}
		nonce := make([]byte, aead.NonceSize())
		dst := make([]byte, 0, len(pt)+aead.Overhead())
		b.SetBytes(int64(len(pt)))
		b.ReportAllocs()
		b.ResetTimer()
		for i := 0; i < b.N; i++ {
			_ = aead.Seal(dst[:0], nonce, pt, aad)
		}
	})

	b.Run("AES256-GCM", func(b *testing.B) {
		c, err := NewAESGCMESPCrypter(256, randBytes(36))
		if err != nil {
			b.Fatal(err)
		}
		dst := make([]byte, 0, len(pt)+c.Overhead())
		b.SetBytes(int64(len(pt)))
		b.ReportAllocs()
		b.ResetTimer()
		for i := 0; i < b.N; i++ {
			if _, err := c.Seal(dst[:0], aad, pt); err != nil {
				b.Fatal(err)
			}
		}
	})
}
