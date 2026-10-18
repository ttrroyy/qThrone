package main

import (
	"math/rand"
	"testing"
	"time"
)

func TestWdttPlusCaptchaFollowsItsOriginalTiming(t *testing.T) {
	if wdttPlusAutoTimeout != 18*time.Second || wdttPlusVisibleTimeout != 180*time.Second {
		t.Fatal("WDTT Plus windows must stay 18 s hidden and 180 s visible")
	}
	if wdttPlusProbes != 16 || wdttPlusProbeInterval != 350*time.Millisecond {
		t.Fatal("WDTT Plus probes the checkbox 16 times 350 ms apart")
	}
	if wdttPlusCheckWatchdog != 3500*time.Millisecond || wdttPlusResultWatchdog != 8*time.Second {
		t.Fatal("WDTT Plus watchdogs are 3.5 s and 8 s")
	}
	random := rand.New(rand.NewSource(1))
	for range 1000 {
		if d := wdttPlusSettle(random); d < 900*time.Millisecond || d >= 1700*time.Millisecond {
			t.Fatalf("settle %s outside 900-1700 ms", d)
		}
		if d := wdttPlusThink(random); d < 600*time.Millisecond || d >= 1300*time.Millisecond {
			t.Fatalf("think %s outside 600-1300 ms", d)
		}
		if d := wdttPlusHold(random); d < 150*time.Millisecond || d >= 320*time.Millisecond {
			t.Fatalf("hold %s outside 150-320 ms", d)
		}
		x, y := wdttPlusClickPoint(random, "icon", 100, 200, 20, 20)
		if x < 105 || x > 115 || y < 205 || y > 215 {
			t.Fatalf("icon touch (%v, %v) outside its middle", x, y)
		}
		x, _ = wdttPlusClickPoint(random, "label", 100, 200, 200, 20)
		if x < 107 || x > 113 {
			t.Fatalf("label touch x=%v is not on the box at its left edge", x)
		}
	}
}

func TestWdttPlusCaptchaVerdictsAndTarget(t *testing.T) {
	for _, status := range []string{"BOT", "error_limit", "ERROR"} {
		if !wdttPlusVerdictFails(status) {
			t.Fatalf("%s must end the attempt", status)
		}
	}
	for _, status := range []string{"", "OK", "SHOW_CAPTCHA"} {
		if wdttPlusVerdictFails(status) {
			t.Fatalf("%q must not end the attempt", status)
		}
	}
	if kind, box := wdttPlusTarget([]any{"icon", 1.0, 2.0, 3.0, 4.0}); kind != "icon" || len(box) != 4 || box[3] != 4 {
		t.Fatalf("icon target read as %q %v", kind, box)
	}
	for _, bad := range []any{nil, "manual", []any{"icon", 1.0}, []any{"other", 1.0, 2.0, 3.0, 4.0}, []any{"label", "x", 2.0, 3.0, 4.0}} {
		if _, box := wdttPlusTarget(bad); box != nil {
			t.Fatalf("%v read as a target", bad)
		}
	}
}
