package main

import (
	"math/rand"
	"testing"
	"time"
)

func TestCaptchaAutoPassFollowsOriginalTiming(t *testing.T) {
	random := rand.New(rand.NewSource(1))
	for range 1000 {
		delay := captchaClickDelay(random)
		if delay < 1070*time.Millisecond || delay > 1880*time.Millisecond {
			t.Fatalf("click delay %s outside the original's 650-1200 + 420-680 ms", delay)
		}
		x, y := captchaClickPoint(random, 100, 200, 40, 20)
		if x < 106 || x > 134 || y < 205 || y > 215 {
			t.Fatalf("touch (%v, %v) outside the inner part of the checkbox", x, y)
		}
	}
}
