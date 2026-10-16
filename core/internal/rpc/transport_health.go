package rpc

import (
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
)

// transportHealth is the watchdog of the original qWDTT and CSQTT apps, fed with
// the helper's public lifecycle lines. URLs, hashes and tokens are never kept.
type transportHealth struct {
	mu    sync.Mutex
	clock func() time.Time
	// qWDTT reports its worker count every few seconds; CSQTT only on change.
	statsClient bool

	startedAt    time.Time
	zeroSince    time.Time
	lastStats    time.Time
	statsSeen    bool
	everActive   bool
	workers      int
	workersKnown bool
	peak         int
	zeroEventAt  time.Time
	restartAt    time.Time
	captchaAt    time.Time
	wrapTimeouts int
}

var activeWorkers = regexp.MustCompile(`(?i)(?:Активных:\s*|\bactive=)([0-9]+)`)

func newTransportHealth(statsClient bool, clock func() time.Time) *transportHealth {
	if clock == nil {
		clock = time.Now
	}
	now := clock()
	return &transportHealth{clock: clock, statsClient: statsClient, startedAt: now, zeroSince: now, lastStats: now}
}

func (h *transportHealth) accept(line string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	now := h.clock()
	if m := activeWorkers.FindStringSubmatch(line); m != nil {
		if count, err := strconv.Atoi(m[1]); err == nil {
			if count == 0 && (!h.workersKnown || h.workers != 0) {
				h.zeroSince = now
			}
			h.workers, h.workersKnown = count, true
			h.lastStats = now
			h.statsSeen = true
			if count > 0 {
				h.everActive = true
				h.wrapTimeouts = 0
			}
		}
	}
	if h.workersKnown {
		h.peak = max(h.peak, h.workers)
	}
	// CSQTT's own events, the ones the original app restarts on.
	if strings.Contains(line, "[CSQTT diagnostic] ACTIVE_ZERO") {
		h.workers, h.workersKnown = 0, true
		h.zeroEventAt = now
	}
	if strings.Contains(line, "[CSQTT diagnostic] SERVER_RESTART") {
		h.restartAt = now
	}
	// A DTLS handshake the server never answers: the password (WRAP key) does not match.
	if strings.Contains(strings.ToUpper(line), "WRAP_AUTH_TIMEOUT") {
		if h.workersKnown && h.workers > 0 {
			h.wrapTimeouts = 0
		} else {
			h.wrapTimeouts++
		}
	}
	lower := strings.ToLower(line)
	if strings.Contains(lower, "captcha") || strings.Contains(lower, "капч") {
		h.captchaAt = now
	}
	if strings.Contains(line, "[READY]") || strings.Contains(line, "[CSQTT diagnostic] READY") || strings.Contains(line, "[CSQTT] SOCKS bridge ready") {
		h.workers, h.workersKnown = max(1, h.workers), true
		h.everActive = true
	}
}

// The original watchdog's verdict: three WRAP timeouts and no worker ever up after 30 s.
func (h *transportHealth) wrongPassword() bool {
	h.mu.Lock()
	defer h.mu.Unlock()
	return !h.everActive && h.wrapTimeouts >= 3 && h.clock().Sub(h.startedAt) > 30*time.Second
}

func (h *transportHealth) needsRestart() bool {
	h.mu.Lock()
	defer h.mu.Unlock()
	now := h.clock()
	// A check the user may be solving is not a lost connection.
	if !h.captchaAt.IsZero() && now.Sub(h.captchaAt) < 5*time.Minute {
		h.zeroSince, h.lastStats = now, now
		return false
	}
	// Original CSQTT: a panel restart of the server, or streams dropped below their peak, after 4 s.
	if !h.restartAt.IsZero() && now.Sub(h.restartAt) >= 4*time.Second {
		return true
	}
	if !h.zeroEventAt.IsZero() && h.peak >= 9 && h.workers < h.peak && now.Sub(h.zeroEventAt) >= 4*time.Second {
		return true
	}
	if h.workers >= h.peak {
		h.zeroEventAt = time.Time{}
	}
	if h.workers > 0 {
		h.zeroSince = now
		// Workers report every few seconds; silence means a hung client (STALE_STATS_MS of the original).
		return h.statsClient && h.statsSeen && now.Sub(h.startedAt) > 120*time.Second && now.Sub(h.lastStats) > 90*time.Second
	}
	// Like the original apps: never restart just because no user traffic flows.
	return h.workersKnown && h.workers == 0 && now.Sub(h.zeroSince) >= 90*time.Second
}
