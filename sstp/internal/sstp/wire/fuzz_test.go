package wire

import "testing"

// Fuzz targets for the SSTP control-message codec.
//
// SSTP runs inside TLS, so a peer has completed a TLS handshake before reaching
// this -- but it has not yet authenticated, and the attribute walk is
// length-prefixed.

func FuzzParseControl(f *testing.F) {
	f.Add([]byte{})
	f.Add(make([]byte, 8))

	f.Fuzz(func(t *testing.T, data []byte) {
		_, _ = ParseControl(data)
	})
}
