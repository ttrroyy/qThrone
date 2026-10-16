package rpc

import (
	"context"
	"crypto/sha256"
	"encoding/json"
	"errors"
	"fmt"
	"path/filepath"
	"strings"
	"time"

	"github.com/google/shlex"
	"qThroneCore/gen"
	"qThroneCore/internal/process"
)

// All entries are owned by the running configuration, under lifecycleMu.
type extraSession struct {
	child     *process.Process
	key       [32]byte
	settings  [32]byte
	transport bool
	tag       string
	release   func()
}

var extraSessions []*extraSession

func isTunnelHelper(path string) bool {
	name := strings.TrimSuffix(strings.ToLower(filepath.Base(path)), ".exe")
	return name == "qwdtt" || name == "openflux" || name == "olcrtc"
}

func stopExtraSessions(tunnelsOnly bool) {
	for i := len(extraSessions) - 1; i >= 0; i-- {
		session := extraSessions[i]
		if tunnelsOnly && !session.transport {
			continue
		}
		session.child.Stop()
		if session.release != nil {
			session.release()
		}
		extraSessions = append(extraSessions[:i], extraSessions[i+1:]...)
	}
}

func transportSettingsKey(config string) [32]byte {
	var c map[string]json.RawMessage
	if json.Unmarshal([]byte(config), &c) != nil || c == nil {
		return [32]byte{}
	}
	// These belong to the local probe process, not the remote authenticated session.
	for _, key := range []string{"socks", "socks_user", "socks_pass", "listen", "probe_only", "interactive_captcha", "ping_timeout_ms", "latency_probe"} {
		delete(c, key)
	}
	encoded, _ := json.Marshal(c)
	return sha256.Sum256(encoded)
}

func activeTransportTag(config string) (string, bool) {
	tag, exists, _ := activeTransport(config)
	return tag, exists
}

func activeTransport(config string) (string, bool, bool) {
	key, valid := qwdttSessionKey(config)
	if !valid {
		return "", false, false
	}
	lifecycleMu.Lock()
	defer lifecycleMu.Unlock()
	for _, session := range extraSessions {
		if session.transport && session.key == key {
			return session.tag, true, session.settings == transportSettingsKey(config)
		}
	}
	return "", false, false
}

func startExtraSessions(ctx context.Context, in *gen.LoadConfigReq) (err error) {
	specs := append([]*gen.RoutedExtraProcess(nil), in.GetRoutedExtraProcesses()...)
	if in.GetNeedExtraProcess() {
		specs = append([]*gen.RoutedExtraProcess{{Path: in.ExtraProcessPath, Args: in.ExtraProcessArgs,
			Config: in.ExtraProcessConf, NoOut: in.ExtraNoOut, OutboundTag: To("proxy")}}, specs...)
	}
	keys := make(map[[32]byte]bool)
	tags := make(map[string]bool)
	// Validate the complete list before starting any process.
	for i, spec := range specs {
		if spec == nil || spec.GetPath() == "" {
			return errors.New("missing extra process path")
		}
		if i >= 1 || !in.GetNeedExtraProcess() {
			if !isTunnelHelper(spec.GetPath()) {
				return errors.New("routing extra process must be a bundled tunnel helper")
			}
		}
		if isTunnelHelper(spec.GetPath()) {
			key, valid := qwdttSessionKey(spec.GetConfig())
			if !valid || spec.GetOutboundTag() == "" {
				return errors.New("invalid routed transport identity")
			}
			if keys[key] {
				return errors.New("the same tunnel device cannot run in two routing profiles")
			}
			if tags[spec.GetOutboundTag()] {
				return errors.New("duplicate routed transport tag")
			}
			keys[key] = true
			tags[spec.GetOutboundTag()] = true
		}
	}
	defer func() {
		if err != nil {
			stopExtraSessions(false)
		}
	}()
	for _, spec := range specs {
		args, e := shlex.Split(spec.GetArgs())
		if e != nil {
			return errors.New("failed to parse extra process arguments")
		}
		session := &extraSession{transport: isTunnelHelper(spec.GetPath()), tag: spec.GetOutboundTag()}
		if session.transport {
			session.key, _ = qwdttSessionKey(spec.GetConfig())
			session.settings = transportSettingsKey(spec.GetConfig())
			// A latency-only test cannot become the user's full-worker connection.
			// Wait for its HTTP request and native leave cleanup, then launch normally.
			gateCtx, cancel := context.WithTimeout(ctx, 330*time.Second)
			session.release, e = acquireQWDTTProbe(gateCtx, session.key)
			cancel()
			if e != nil {
				return errors.New("timed out waiting for the previous tunnel test to finish cleanup")
			}
		}
		confPath, folder, e := process.CreateExtraConfig(spec.GetConfig())
		if e != nil {
			if session.release != nil {
				session.release()
			}
			return errors.New("failed to create private extra process configuration")
		}
		for idx, arg := range args {
			if strings.Contains(arg, "%s") {
				args[idx] = fmt.Sprintf(arg, confPath)
				break
			}
		}
		session.child = process.NewProcess(spec.GetPath(), args, spec.GetNoOut())
		if session.transport && session.tag != "proxy" {
			session.child.SetRoutedTransport()
		}
		session.child.SetCleanupPath(folder)
		if session.transport {
			session.child.EnableStdinShutdown("STOP")
		}
		extraSessions = append(extraSessions, session)
		if e := session.child.Start(); e != nil {
			return e
		}
	}
	return nil
}
