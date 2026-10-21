package csqtt

import (
	"strings"
	"testing"
)

func TestLegacySharePayloadMatchesMobileAESGCM(t *testing.T) {
	// Independent fixture shared with qThroneMobile, using the Legacy key and nonce layout.
	const link = "csqtt://AAECAwQFBgcICQoLnWJ4sGLhXEU4Lx5kmNv4OgG3nIAwttrQ9J8P4vf_mYSVcA21sCLBNlFIYwCVOGm4NBpIOEwOveKOn3k9OmGrnU1kpW32LX1P0DYwJIoD72fH_zBQH5eZPx1htJPKCYwG_P6KzrnibBCrY2UYCouGNKq--cwoSbO-Zv1WI6UBk5mQ98ct_g"
	plain, err := SharePayload(link, false)
	if err != nil || !strings.Contains(plain, "host=198.51.100.25") || !strings.Contains(plain, "Pre250-Secret") {
		t.Fatal("AES-GCM mobile fixture rejected")
	}
	encoded, err := SharePayload(plain, true)
	if err != nil {
		t.Fatal(err)
	}
	again, err := SharePayload(encoded, false)
	if err != nil || again != plain {
		t.Fatal("share round trip changed payload")
	}
	other, _ := SharePayload(plain, true)
	if other != encoded {
		t.Fatal("Legacy deterministic HMAC nonce changed")
	}
	for _, bad := range []string{link[:len(link)-5], link[:80] + "A" + link[81:], "csqtt://short", strings.Repeat("a", 65537)} {
		if _, err := SharePayload(bad, false); err == nil {
			t.Fatal("invalid share accepted")
		}
	}
}

func TestLegacyNegotiatedWorkers(t *testing.T) {
	base := "TUNCONF:10.66.67.2:1.1.1.1:9000:stream-v1"
	for _, suffix := range []string{"", ":workers=5", ":workers=100"} {
		if _, err := ParseTunnelConfig(base + suffix); err != nil {
			t.Fatal(err)
		}
	}
	for _, suffix := range []string{":workers=-1", ":workers=101", ":workers=no", ":unexpected=5", ":workers=5:extra"} {
		if _, err := ParseTunnelConfig(base + suffix); err == nil {
			t.Fatal("invalid negotiation accepted")
		}
	}
}
