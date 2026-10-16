package main

import (
	"encoding/json"
	"math"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestCSQTTGenerationFollowsOriginalApp(t *testing.T) {
	now := time.Unix(1_800_000_000, 0)
	if got := nextCSQTTGeneration(now, 0); got != 1_800_000_000 {
		t.Fatalf("first generation %d is not the clock", got)
	}
	if got := nextCSQTTGeneration(now, 1_800_000_000); got != 1_800_000_001 {
		t.Fatalf("generation %d did not grow past the last one", got)
	}
	if got := nextCSQTTGeneration(now, math.MaxUint64); got != 1_800_000_000 {
		t.Fatalf("overflow handled as %d", got)
	}
}

func TestEveryCSQTTLaunchGetsItsOwnEpoch(t *testing.T) {
	state := t.TempDir()
	private := filepath.Join(t.TempDir(), "config.json")
	if err := os.WriteFile(private, []byte(`{"peer":"server.invalid:46000","password":"secret","device_id":"device"}`), 0600); err != nil {
		t.Fatal(err)
	}
	now := time.Unix(1_800_000_000, 0)
	read := func(path string) (uint64, string) {
		data, err := os.ReadFile(path)
		if err != nil {
			t.Fatal(err)
		}
		var c struct {
			Generation uint64 `json:"generation"`
			Salt       string `json:"salt"`
			Password   string `json:"password"`
		}
		if json.Unmarshal(data, &c) != nil || c.Password != "secret" {
			t.Fatal("launch configuration lost the profile")
		}
		return c.Generation, c.Salt
	}
	first, err := csqttLaunchConfig(private, state, now)
	if err != nil {
		t.Fatal(err)
	}
	second, err := csqttLaunchConfig(private, state, now)
	if err != nil {
		t.Fatal(err)
	}
	g1, s1 := read(first)
	g2, s2 := read(second)
	if g1 != 1_800_000_000 || g2 != g1+1 || len(s1) != 32 || s1 == s2 {
		t.Fatalf("launches share an epoch: %d/%s %d/%s", g1, s1, g2, s2)
	}
	// Kept across restarts of the client.
	third, err := csqttLaunchConfig(private, state, now.Add(-time.Hour))
	if err != nil {
		t.Fatal(err)
	}
	if g3, _ := read(third); g3 != g2+1 {
		t.Fatalf("generation %d went back after a restart", g3)
	}
	if data, _ := os.ReadFile(private); string(data) != `{"peer":"server.invalid:46000","password":"secret","device_id":"device"}` {
		t.Fatal("the core's private configuration was rewritten")
	}
}
