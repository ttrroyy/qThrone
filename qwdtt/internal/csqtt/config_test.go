package csqtt

import (
	"strings"
	"testing"
)

func TestOriginalLinks(t *testing.T) {
	for _, raw := range []string{
		"csqtt://connect?v=2&host=203.0.113.7&peer=46000&password=p%40ss",
		"csqtt://connect?v=2host=203.0.113.7peer=46000password=p%40ss",
		"csqtt://p%40ss@203.0.113.7:46000",
	} {
		l, err := ParseLink(raw)
		if err != nil || l.Peer() != "203.0.113.7:46000" || l.Password != "p@ss" {
			t.Fatal("original link not accepted")
		}
	}
	l, err := ParseLink("csqtt://connect?v=2&host=2001%3Adb8%3A%3A1&peer=46000&password=secret&hashes=abcdefghijklmno%2Bp+abcdefghijklmnop2")
	if err != nil || l.Peer() != "[2001:db8::1]:46000" || len(l.Hashes) != 2 || l.Hashes[0] != "abcdefghijklmno+p" {
		t.Fatal("IPv6 or encoded plus lost")
	}
}

func TestInvalidLinks(t *testing.T) {
	base := "csqtt://connect?v=2&host=203.0.113.7&peer=46000&password=secret"
	for _, raw := range []string{
		strings.Replace(base, "v=2", "v=3", 1),
		strings.Replace(base, "peer=46000", "peer=0", 1),
		base + "&hashes=", base + "&hashes=short", base + "#name",
		base + "&hashes=abcdefghijklmnop1+abcdefghijklmnop1",
		base + "&hashes=abcdefghijklmnop1+abcdefghijklmnop2+abcdefghijklmnop3+abcdefghijklmnop4+abcdefghijklmnop5+abcdefghijklmnop6+abcdefghijklmnop7",
	} {
		if _, err := ParseLink(raw); err == nil {
			t.Fatal("invalid link accepted")
		}
	}
}

func TestWorkerLimits(t *testing.T) {
	for hashes, want := range []int{27, 27, 54, 81, 108, 126, 126} {
		if got := NormalizeWorkers(200, hashes, false); got != want {
			t.Fatalf("hashes %d: got %d want %d", hashes, got, want)
		}
	}
	if NormalizeWorkers(16, 1, false) != 9 || NormalizeWorkers(126, 1, true) != 126 {
		t.Fatal("worker normalization")
	}
}

func TestTunnelPortIsNotMTU(t *testing.T) {
	c, err := ParseTunnelConfig("TUNCONF:10.66.67.42:8.8.8.8,8.8.4.4:19000:stream-v2")
	if err != nil || c.ClientPort != 19000 || len(c.DNS) != 2 {
		t.Fatal("UDP port misread")
	}
	c, err = ParseTunnelConfig("TUNCONF:10.66.67.42:1.1.1.1:0:stream-v2")
	if err != nil || c.ClientPort != 0 {
		t.Fatal("original UDS config rejected")
	}
}
