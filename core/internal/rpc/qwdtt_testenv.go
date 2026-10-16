package rpc

import (
	"context"
	"crypto/sha256"
	"encoding/json"
	"errors"
	"net"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"

	"qThroneCore/internal/process"
)

// Probe sidecars are session-owned; never replace the active extraProcess.
type qwdttProbeGate struct {
	slot  chan struct{}
	users int
}

var qwdttProbeMu sync.Mutex
var qwdttProbeGates = make(map[[32]byte]*qwdttProbeGate)

func qwdttSessionKey(config string) ([32]byte, bool) {
	var c struct {
		Peer     string `json:"peer"`
		Backend  string `json:"backend"`
		DeviceID string `json:"device_id"`
	}
	if json.Unmarshal([]byte(config), &c) != nil || c.Peer == "" || c.DeviceID == "" {
		return [32]byte{}, false
	}
	return sha256.Sum256([]byte(c.Backend + "\x00" + c.Peer + "\x00" + c.DeviceID)), true
}

// Separate devices/servers can run together. Duplicate profiles must not share
// the server-assigned IP between two independent client netstacks.
func acquireQWDTTProbe(ctx context.Context, key [32]byte) (func(), error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	qwdttProbeMu.Lock()
	gate := qwdttProbeGates[key]
	if gate == nil {
		gate = &qwdttProbeGate{slot: make(chan struct{}, 1)}
		qwdttProbeGates[key] = gate
	}
	gate.users++
	qwdttProbeMu.Unlock()
	forget := func() {
		qwdttProbeMu.Lock()
		gate.users--
		if gate.users == 0 {
			delete(qwdttProbeGates, key)
		}
		qwdttProbeMu.Unlock()
	}
	select {
	case gate.slot <- struct{}{}:
		var once sync.Once
		return func() { once.Do(func() { <-gate.slot; forget() }) }, nil
	case <-ctx.Done():
		forget()
		return nil, ctx.Err()
	}
}

// Cap fresh VK-authenticated test sessions across both transports. A permit
// remains owned until native shutdown/leave cleanup has completed.
var transportProbeSlots = make(chan struct{}, 3)

func acquireTransportProbeSlot(ctx context.Context) (func(), error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	select {
	case transportProbeSlots <- struct{}{}:
		var once sync.Once
		return func() { once.Do(func() { <-transportProbeSlots }) }, nil
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

func prepareQWDTTProbe(ctx context.Context, config string) (func(), error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	if len(config) > 65536 {
		return nil, errors.New("qWDTT test config is too large")
	}
	var c struct {
		SOCKS       string `json:"socks"`
		Peer        string `json:"peer"`
		Backend     string `json:"backend"`
		DeviceID    string `json:"device_id"`
		Interactive bool   `json:"interactive_captcha"`
	}
	if json.Unmarshal([]byte(config), &c) != nil {
		return nil, errors.New("invalid qWDTT test config")
	}
	// Include queue waits in the startup budget; manual captcha gets time for user input.
	budget := 5 * time.Minute
	if c.Backend == "openflux" || c.Backend == "olcrtc" {
		budget = 15 * time.Second
	}
	if c.Backend == "olcrtc" {
		budget = 30 * time.Second
	}
	ctx, cancelProbe := context.WithTimeout(ctx, budget)
	defer cancelProbe()
	host, port, err := net.SplitHostPort(c.SOCKS)
	n, _ := strconv.Atoi(port)
	if err != nil || host != "127.0.0.1" || n < 1 || n > 65535 {
		return nil, errors.New("invalid qWDTT test listener")
	}
	key, validKey := qwdttSessionKey(config)
	if !validKey {
		return nil, errors.New("invalid qWDTT test session identity")
	}
	_, conflicts := activeTransportTag(config)
	if conflicts {
		return nil, errors.New("this qWDTT device is already connected; test its active profile instead")
	}
	queueBudget := 5 * time.Minute
	if c.Backend == "openflux" || c.Backend == "olcrtc" {
		queueBudget = 15 * time.Second
	}
	queueCtx, cancelQueue := context.WithTimeout(ctx, queueBudget)
	defer cancelQueue()
	release, err := acquireQWDTTProbe(queueCtx, key)
	if err != nil {
		return nil, err
	}
	if c.Backend == "" || c.Backend == "csqtt" {
		slotRelease, slotErr := acquireTransportProbeSlot(queueCtx)
		if slotErr != nil {
			release()
			return nil, slotErr
		}
		deviceRelease := release
		release = func() { slotRelease(); deviceRelease() }
	}
	var interaction struct {
		Interactive bool `json:"interactive_captcha"`
	}
	_ = json.Unmarshal([]byte(config), &interaction)
	if interaction.Interactive {
		// One interactive probe owns the browser at a time, including across servers.
		browserRelease, browserErr := acquireQWDTTProbe(queueCtx, sha256.Sum256([]byte("qThrone interactive captcha browser")))
		if browserErr != nil {
			release()
			return nil, browserErr
		}
		sessionRelease := release
		release = func() { browserRelease(); sessionRelease() }
	}
	executable, err := os.Executable()
	if err != nil {
		release()
		return nil, err
	}
	name := "qwdtt"
	if c.Backend == "openflux" || c.Backend == "olcrtc" {
		name = c.Backend
	}
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	config, err = noninteractiveQWDTTConfig(config)
	if err != nil {
		release()
		return nil, err
	}
	path, folder, err := process.CreateExtraConfig(config)
	if err != nil {
		release()
		return nil, err
	}
	argument := "-config"
	if c.Backend == "csqtt" {
		argument = "-csqtt-config"
	} else if c.Backend == "openflux" || c.Backend == "olcrtc" {
		argument = "--qthrone-config"
	} else if c.Backend != "" {
		release()
		os.RemoveAll(folder)
		return nil, errors.New("unknown probe transport")
	}
	child := process.NewProcess(filepath.Join(filepath.Dir(executable), name), []string{argument, path}, false)
	if c.Backend == "openflux" || c.Backend == "olcrtc" {
		child = process.NewProcess(filepath.Join(filepath.Dir(executable), name), []string{argument, path}, true)
	}
	child.SetBackgroundProbe()
	child.SetCleanupPath(folder)
	child.EnableStdinShutdown("STOP")
	captchaRequired := make(chan struct{}, 1)
	if c.Backend == "csqtt" {
		var outputMu sync.Mutex
		var pending string
		var captchaOnce sync.Once
		child.SetOutputObserver(func(b []byte) {
			outputMu.Lock()
			defer outputMu.Unlock()
			pending += string(b)
			if strings.Contains(pending, "[CSQTT] CAPTCHA_REQUIRED") {
				captchaOnce.Do(func() { captchaRequired <- struct{}{} })
			}
			if len(pending) > 256 {
				pending = pending[len(pending)-256:]
			}
		})
	}
	if err = child.Start(); err != nil {
		release()
		return nil, err
	}
	// Native VK call cleanup can take 20 seconds. Return the test result now,
	// but retain the device gate until the child has actually stopped.
	cleanup := deferredProbeCleanup(child.Stop, release)
	readyCtx, cancel := context.WithTimeout(ctx, budget)
	defer cancel()
	startupBudget := 5 * time.Minute
	if c.Backend == "openflux" || c.Backend == "olcrtc" {
		startupBudget = 15 * time.Second
	}
	if c.Backend == "olcrtc" {
		startupBudget = 30 * time.Second
	}
	startupTimer := time.NewTimer(startupBudget)
	defer startupTimer.Stop()
	for {
		conn, e := (&net.Dialer{Timeout: 100 * time.Millisecond}).DialContext(readyCtx, "tcp", c.SOCKS)
		if e == nil {
			conn.Close()
			return cleanup, nil
		}
		select {
		case <-captchaRequired:
			if c.Interactive {
				if !startupTimer.Stop() {
					select {
					case <-startupTimer.C:
					default:
					}
				}
				startupTimer.Reset(5 * time.Minute)
			}
		case <-startupTimer.C:
			cleanup()
			return nil, errors.New("transport test startup timed out")
		case <-child.Done():
			cleanup()
			return nil, errors.New("qWDTT authentication or bridge startup failed; connect interactively and retry the test")
		case <-readyCtx.Done():
			cleanup()
			if errors.Is(readyCtx.Err(), context.Canceled) {
				return nil, context.Canceled
			}
			return nil, errors.New("qWDTT test cancelled or bridge startup timed out")
		case <-time.After(100 * time.Millisecond):
		}
	}
}

func deferredProbeCleanup(stop, release func()) func() {
	var once sync.Once
	return func() { once.Do(func() { go func() { stop(); release() }() }) }
}

func noninteractiveQWDTTConfig(config string) (string, error) {
	var c map[string]json.RawMessage
	if err := json.Unmarshal([]byte(config), &c); err != nil || c == nil {
		return "", errors.New("invalid qWDTT test config")
	}
	c["probe_only"] = json.RawMessage("true")
	var interactive bool
	_ = json.Unmarshal(c["interactive_captcha"], &interactive)
	c["interactive_captcha"] = json.RawMessage(strconv.FormatBool(interactive))
	// Keep the profile's automatic solver chain. Interactive tests permit its
	// browser fallback; they must not force every challenge into a visible window.
	b, err := json.Marshal(c)
	return string(b), err
}
