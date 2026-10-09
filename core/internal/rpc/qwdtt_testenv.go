package rpc

import (
	"context"
	"encoding/json"
	"errors"
	"net"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"

	"ThroneCore/internal/process"
)

// Probe sidecars are session-owned; never replace the active extraProcess.
var qwdttProbeSlot = make(chan struct{}, 1)

func prepareQWDTTProbe(ctx context.Context, config string) (func(), error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	lifecycleMu.Lock()
	active := extraProcess != nil && strings.TrimSuffix(strings.ToLower(filepath.Base(extraProcess.ExecutablePath())), ".exe") == "qwdtt"
	lifecycleMu.Unlock()
	if active {
		return nil, errors.New("stop the active qWDTT connection before testing another qWDTT profile")
	}

	if len(config) > 65536 {
		return nil, errors.New("qWDTT test config is too large")
	}
	var c struct {
		SOCKS string `json:"socks"`
	}
	if json.Unmarshal([]byte(config), &c) != nil {
		return nil, errors.New("invalid qWDTT test config")
	}
	host, port, err := net.SplitHostPort(c.SOCKS)
	n, _ := strconv.Atoi(port)
	if err != nil || host != "127.0.0.1" || n < 1 || n > 65535 {
		return nil, errors.New("invalid qWDTT test listener")
	}
	select {
	case qwdttProbeSlot <- struct{}{}:
	case <-ctx.Done():
		return nil, ctx.Err()
	}
	release := func() { <-qwdttProbeSlot }
	executable, err := os.Executable()
	if err != nil {
		release()
		return nil, err
	}
	name := "qwdtt"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	path, folder, err := process.CreateExtraConfig(config)
	if err != nil {
		release()
		return nil, err
	}
	child := process.NewProcess(filepath.Join(filepath.Dir(executable), name), []string{"-config", path}, false)
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
			return nil, errors.New("qWDTT exited before its SOCKS bridge was ready")
		case <-readyCtx.Done():
			cleanup()
			return nil, errors.New("qWDTT test cancelled or bridge startup timed out")
		case <-time.After(100 * time.Millisecond):
		}
	}
}
