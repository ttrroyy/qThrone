package rpc

import (
	"context"
	"crypto/sha256"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"

	"qThroneCore/internal/probe"
	"qThroneCore/internal/process"
)

const (
	qwdttHandshakeBudget = 15 * time.Second
	// Matches desktopPingAuthBudget of the bundled client.
	qwdttPingAuthBudget = 90 * time.Second
)

// Only qWDTT has the native TURN/DTLS handshake probe. Other backends retain URL tests.
func nativeQWDTTPing(config string) bool {
	var c struct {
		Backend string `json:"backend"`
	}
	return strings.HasPrefix(strings.TrimSpace(config), "{") && json.Unmarshal([]byte(config), &c) == nil && c.Backend == ""
}

func runQWDTTPing(parent context.Context, config string, budget ...time.Duration) (int32, error) {
	if len(config) > 65536 {
		return 0, errors.New("test config is too large")
	}
	key, valid := qwdttSessionKey(config)
	if !valid {
		return 0, errors.New("invalid test session identity")
	}
	// The TURN allocation and DTLS handshake get the original app's 15 s; a 3 s
	// HTTP timeout failed working servers on slower networks.
	timeout := qwdttHandshakeBudget
	if len(budget) > 0 && budget[0] > timeout {
		timeout = budget[0]
	}
	stopped, cancelStopped := context.WithCancel(parent)
	defer cancelStopped()
	stop := context.AfterFunc(probe.TestContext(), cancelStopped)
	defer stop()
	queueCtx, cancelQueue := context.WithTimeout(stopped, nativeProbeQueueBudget)
	defer cancelQueue()
	// A handshake probe never authenticates a second netstack as the same device.
	// Use a separate gate so testing the active profile cannot wait on its own VPN.
	pingKey := sha256.Sum256(append([]byte("qwdtt native ping\x00"), key[:]...))
	release, err := acquireQWDTTProbe(queueCtx, pingKey)
	if err != nil {
		return 0, err
	}
	defer release()
	// Profiles of one subscription join the same VK calls; parallel joins of one call fail some of them.
	if callKey, shared := qwdttCallKey(config); shared {
		callRelease, err := acquireQWDTTProbe(queueCtx, callKey)
		if err != nil {
			return 0, err
		}
		defer callRelease()
	}
	// qWDTT and CSQTT probes share one budget, so a sweep stays below VK's limits.
	slotRelease, err := acquireTransportProbeSlot(queueCtx)
	if err != nil {
		return 0, err
	}
	defer slotRelease()
	var c struct {
		Interactive bool `json:"interactive_captcha"`
	}
	_ = json.Unmarshal([]byte(config), &c)
	if c.Interactive {
		browserRelease, err := acquireQWDTTProbe(queueCtx, sha256.Sum256([]byte("qThrone interactive captcha browser")))
		if err != nil {
			return 0, err
		}
		defer browserRelease()
	}
	// The budget starts once the ping holds its gates: waiting for another profile's turn says nothing
	// about this server. VK authorization (with automatic checks) is preparation, not the measured round trip.
	ctx, cancel := context.WithTimeout(stopped, timeout+qwdttPingAuthBudget+2*time.Second)
	defer cancel()
	var pingConfig map[string]any
	if err = json.Unmarshal([]byte(config), &pingConfig); err != nil {
		return 0, err
	}
	pingConfig["mode"] = "wg"
	pingConfig["probe_only"] = true
	pingConfig["ping_timeout_ms"] = timeout.Milliseconds()
	pingConfig["interactive_captcha"] = false
	encoded, err := json.Marshal(pingConfig)
	if err != nil {
		return 0, err
	}
	config, err = noninteractiveQWDTTConfig(string(encoded))
	if err != nil {
		return 0, err
	}
	path, folder, err := process.CreateExtraConfig(config)
	if err != nil {
		return 0, err
	}
	defer os.RemoveAll(folder)
	executable, err := os.Executable()
	if err != nil {
		return 0, err
	}
	name := "qwdtt"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	return executeQWDTTPing(ctx, filepath.Join(filepath.Dir(executable), name), []string{"--config", path, "--ping-only"})
}

// Parse stdout incrementally: writes may split a result or combine it with logs.
type qwdttPingOutput struct {
	mu            sync.Mutex
	pending       string
	delay         int32
	found         bool
	failed        bool
	failureReason string
}

func (p *qwdttPingOutput) write(b []byte) {
	p.mu.Lock()
	defer p.mu.Unlock()
	for _, ch := range b {
		if ch == '\n' {
			line := strings.TrimSpace(p.pending)
			if strings.HasPrefix(line, "PING_RESULT|") {
				value, err := strconv.ParseInt(strings.TrimPrefix(line, "PING_RESULT|"), 10, 32)
				if err == nil && value >= 0 {
					if value == 0 {
						value = 1
					}
					p.delay, p.found = int32(value), true
				} else {
					p.failed = true
				}
			} else if strings.HasPrefix(line, "PING_ERROR|") {
				// Do not return arbitrary authentication logs or credentials to the UI.
				p.failed = true
				if strings.HasPrefix(line, "PING_ERROR|VK authorization:") {
					p.failureReason = "VK authorization failed"
				}
				if strings.HasPrefix(line, "PING_ERROR|TURN/DTLS:") {
					p.failureReason = "TURN/DTLS server probe failed"
				}
			}
			p.pending = ""
		} else if len(p.pending) < 1024 {
			p.pending += string(ch)
		}
	}
}

func executeQWDTTPing(ctx context.Context, executable string, args []string) (int32, error) {
	if err := ctx.Err(); err != nil {
		return 0, err
	}
	var output qwdttPingOutput
	child := process.NewProcess(executable, args, true)
	child.SetBackgroundProbe()
	child.EnableStdinShutdown("STOP")
	child.SetOutputObserver(output.write)
	if err := child.Start(); err != nil {
		return 0, err
	}
	defer child.Stop()
	select {
	case <-ctx.Done():
		return 0, ctx.Err()
	case <-child.Done():
		if err := ctx.Err(); err != nil {
			return 0, err
		}
		output.mu.Lock()
		defer output.mu.Unlock()
		if output.found && !output.failed {
			return output.delay, nil
		}
		if output.failureReason != "" {
			return 0, errors.New(output.failureReason)
		}
		return 0, errors.New("TURN/DTLS handshake did not return a valid ping result")
	}
}
