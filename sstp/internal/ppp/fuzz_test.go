package ppp

import "testing"

// Fuzz targets for the PPP control-protocol codec.
//
// LCP and IPCP options are parsed from whatever the peer sends, and on the
// server side that happens before authentication completes — a client can send
// a Configure-Request before it has proved anything. The option walk is
// length-prefixed and therefore the usual hazard.

func FuzzParseCP(f *testing.F) {
	f.Add([]byte{})
	f.Add([]byte{0x01, 0x01, 0x00, 0x04})

	f.Fuzz(func(t *testing.T, data []byte) {
		pkt, ok := parseCP(data)
		if !ok {
			return
		}
		_, _ = parseOptions(pkt.Body)
	})
}

func FuzzParseOptions(f *testing.F) {
	f.Add([]byte{})
	f.Add([]byte{0x01, 0x04, 0x05, 0xd4})

	f.Fuzz(func(t *testing.T, data []byte) {
		_, _ = parseOptions(data)
	})
}

// The MS-CHAPv2 bodies are parsed on both roles before the exchange completes.
func FuzzParseCHAP(f *testing.F) {
	f.Add([]byte{})
	f.Add(make([]byte, 49))

	f.Fuzz(func(t *testing.T, data []byte) {
		_, _, _ = parseChallenge(data)
		_, _, _, _ = parseResponse(data)
	})
}
