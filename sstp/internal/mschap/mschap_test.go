package mschap

import (
	"bytes"
	"encoding/hex"
	"testing"
)

// The known-answer vectors are from RFC 2759 section 9.2 (MS-CHAPv2) and the
// MPPE derivation in RFC 3079. They pin the whole chain — NT hash, challenge
// hash, NT response, authenticator response, and the master key — so a wrong
// endianness or magic constant fails loudly here rather than at interop.
var (
	vecUser     = "User"
	vecPassword = "clientPass"
	vecAuthCh   = mustHex16("5B5D7C7D7B3F2F3E3C2C602132262628")
	vecPeerCh   = mustHex16("21402324255E262A28295F2B3A337C7E")
)

func mustHex16(s string) [16]byte {
	b, err := hex.DecodeString(s)
	if err != nil || len(b) != 16 {
		panic("bad test vector: " + s)
	}
	var out [16]byte
	copy(out[:], b)
	return out
}

func TestNTPasswordHash(t *testing.T) {
	got := NTPasswordHash(vecPassword)
	// RFC 2759 section 9.2.2: PasswordHash = 44 EB BA 8D 53 12 B8 D6 11 47 44 11
	// F5 69 89 AE.
	want := "44EBBA8D5312B8D611474411F56989AE"
	if hexUpper(got[:]) != want {
		t.Errorf("NTPasswordHash = %s, want %s", hexUpper(got[:]), want)
	}
}

func TestChallengeHash(t *testing.T) {
	got := ChallengeHash(vecPeerCh, vecAuthCh, vecUser)
	want := "D02E4386BCE91226"
	if hexUpper(got[:]) != want {
		t.Errorf("ChallengeHash = %s, want %s", hexUpper(got[:]), want)
	}
}

func TestGenerateNTResponse(t *testing.T) {
	got := GenerateNTResponse(vecAuthCh, vecPeerCh, vecUser, vecPassword)
	want := "82309ECD8D708B5EA08FAA3981CD83544233114A3D85D6DF"
	if hexUpper(got[:]) != want {
		t.Errorf("GenerateNTResponse = %s, want %s", hexUpper(got[:]), want)
	}
}

func TestAuthenticatorResponse(t *testing.T) {
	ntResp := GenerateNTResponse(vecAuthCh, vecPeerCh, vecUser, vecPassword)
	got := AuthenticatorResponse(vecAuthCh, vecPeerCh, vecUser, vecPassword, ntResp)
	want := "S=407A5589115FD0D6209F510FE9C04566932CDA56"
	if got != want {
		t.Errorf("AuthenticatorResponse = %s, want %s", got, want)
	}
}

func TestMasterKey(t *testing.T) {
	// RFC 3079 section 4.5.1: with this password and NT response the MasterKey is
	// FDECE3717A8C838CB388E527AE3CDD31.
	ntResp := GenerateNTResponse(vecAuthCh, vecPeerCh, vecUser, vecPassword)
	ph := NTPasswordHash(vecPassword)
	phh := ntPasswordHashHash(ph)
	mk := getMasterKey(phh, ntResp)
	want := "FDECE3717A8C838CB388E527AE3CDD31"
	if hexUpper(mk[:]) != want {
		t.Errorf("MasterKey = %s, want %s", hexUpper(mk[:]), want)
	}
}

func TestClientHLAKLength(t *testing.T) {
	ntResp := GenerateNTResponse(vecAuthCh, vecPeerCh, vecUser, vecPassword)
	hlak := ClientHLAK(vecPassword, ntResp)
	if len(hlak) != HLAKLen {
		t.Fatalf("HLAK length = %d, want %d", len(hlak), HLAKLen)
	}
	// The two halves are the distinct send and receive keys, so they must differ.
	if bytes.Equal(hlak[:16], hlak[16:]) {
		t.Error("HLAK send and receive halves are identical")
	}
}

func hexUpper(b []byte) string {
	const d = "0123456789ABCDEF"
	out := make([]byte, len(b)*2)
	for i, c := range b {
		out[i*2] = d[c>>4]
		out[i*2+1] = d[c&0xf]
	}
	return string(out)
}
