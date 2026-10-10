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

	"ThroneCore/internal/process"
)

// Probe sidecars are session-owned; never replace the active extraProcess.
type qwdttProbeGate struct {
	slot  chan struct{}
	users int
}

var qwdttProbeMu sync.Mutex
var qwdttProbeGates = make(map[[32]byte]*qwdttProbeGate)

// Protected by lifecycleMu; only identifies the active qWDTT session.
var activeQWDTTProbeKey [32]byte
var activeQWDTTProbeKeyValid bool

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

func prepareQWDTTProbe(ctx context.Context, config string) (func(), error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	if len(config) > 65536 {
		return nil, errors.New("qWDTT test config is too large")
	}
	var c struct {
		SOCKS    string `json:"socks"`
		Peer     string `json:"peer"`
		Backend  string `json:"backend"`
		DeviceID string `json:"device_id"`
	}
	if json.Unmarshal([]byte(config), &c) != nil {
		return nil, errors.New("invalid qWDTT test config")
	}
	host, port, err := net.SplitHostPort(c.SOCKS)
	n, _ := strconv.Atoi(port)
	if err != nil || host != "127.0.0.1" || n < 1 || n > 65535 {
		return nil, errors.New("invalid qWDTT test listener")
	}
	key, validKey := qwdttSessionKey(config)
	if !validKey {
		return nil, errors.New("invalid qWDTT test session identity")
	}
	lifecycleMu.Lock()
	active := extraProcess != nil && strings.TrimSuffix(strings.ToLower(filepath.Base(extraProcess.ExecutablePath())), ".exe") == "qwdtt"
	conflicts := active && (!activeQWDTTProbeKeyValid || activeQWDTTProbeKey == key)
	lifecycleMu.Unlock()
	if conflicts {
		return nil, errors.New("this qWDTT device is already connected; test its active profile instead")
	}
	release, err := acquireQWDTTProbe(ctx, key)
	if err != nil {
		return nil, err
	}
	var interaction struct {
		Interactive bool `json:"interactive_captcha"`
	}
	_ = json.Unmarshal([]byte(config), &interaction)
	if interaction.Interactive {
		// One interactive probe owns the browser at a time, including across servers.
		browserRelease, browserErr := acquireQWDTTProbe(ctx, sha256.Sum256([]byte("qThrone interactive captcha browser")))
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
	} else if c.Backend != "" {
		release()
		os.RemoveAll(folder)
		return nil, errors.New("unknown probe transport")
	}
	child := process.NewProcess(filepath.Join(filepath.Dir(executable), name), []string{argument, path}, false)
	child.SetBackgroundProbe()
	child.SetCleanupPath(folder)
	child.EnableStdinShutdown("STOP")
	if err = child.Start(); err != nil {
		release()
		return nil, err
	}
	cleanup := func() { child.Stop(); release() }
	readyCtx, cancel := context.WithTimeout(ctx, 3*time.Minute)
	defer cancel()
	for {
		conn, e := (&net.Dialer{Timeout: 100 * time.Millisecond}).DialContext(readyCtx, "tcp", c.SOCKS)
		if e == nil {
			conn.Close()
			return cleanup, nil
		}
		select {
		case <-child.Done():
			cleanup()
			return nil, errors.New("qWDTT authentication or bridge startup failed; connect interactively and retry the test")
		case <-readyCtx.Done():
			cleanup()
			return nil, errors.New("qWDTT test cancelled or bridge startup timed out")
		case <-time.After(100 * time.Millisecond):
		}
	}
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
	if interactive {
		// Open the WebView directly when a manual test encounters a captcha.
		c["captcha_mode"] = json.RawMessage(`"wv"`)
	}
	b, err := json.Marshal(c)
	return string(b), err
}
