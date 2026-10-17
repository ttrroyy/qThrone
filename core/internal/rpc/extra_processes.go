package rpc

import (
	"context"
	"crypto/sha256"
	"encoding/json"
	"errors"
	"fmt"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/google/shlex"
	"qThroneCore/gen"
	"qThroneCore/internal/process"
)

// All entries are owned by the running configuration, under lifecycleMu.
// A transport's child is replaced by its watchdog, under the session's own lock.
type extraSession struct {
	mu        sync.Mutex
	child     *process.Process
	health    *transportHealth
	key       [32]byte
	settings  [32]byte
	transport bool
	backend   string
	tag       string
	release   func()
	launch    func() (*process.Process, *transportHealth, error)
	config    string
	stopping  chan struct{}
	stopOnce  sync.Once
	watchDone chan struct{}
}

var extraSessions []*extraSession

func isTunnelHelper(path string) bool {
	name := strings.TrimSuffix(strings.ToLower(filepath.Base(path)), ".exe")
	return tunnelHelpers[name]
}

func transportBackend(config string) string {
	var c struct {
		Backend string `json:"backend"`
	}
	_ = json.Unmarshal([]byte(config), &c)
	return c.Backend
}

// stop ends the watchdog first, so it cannot start a replacement for the child being stopped.
func (s *extraSession) stop() {
	if s.stopping != nil {
		s.stopOnce.Do(func() { close(s.stopping) })
	}
	s.mu.Lock()
	child := s.child
	s.mu.Unlock()
	if child != nil {
		child.Stop()
	}
	if s.watchDone != nil {
		<-s.watchDone
	}
	// The watchdog may have been between Stop and Start of a replacement.
	s.mu.Lock()
	if s.child != nil && s.child != child {
		s.child.Stop()
	}
	s.mu.Unlock()
	if s.release != nil {
		s.release()
	}
}

func stopExtraSessions(tunnelsOnly bool) {
	for i := len(extraSessions) - 1; i >= 0; i-- {
		session := extraSessions[i]
		if tunnelsOnly && !session.transport {
			continue
		}
		session.stop()
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

// An OpenFlux exit serves one client at a time and hands the session to the newest
// one, so a probe of another profile on any carrier the connection rides would drop it.
func activeOpenFluxCarrier(config string) bool {
	carriers := openFluxCarriers(config)
	if len(carriers) == 0 {
		return false
	}
	lifecycleMu.Lock()
	defer lifecycleMu.Unlock()
	for _, session := range extraSessions {
		if !session.transport || session.backend != "openflux" {
			continue
		}
		for carrier := range openFluxCarriers(session.config) {
			if carriers[carrier] {
				return true
			}
		}
	}
	return false
}

func openFluxCarriers(config string) map[string]bool {
	var c struct {
		Backend    string `json:"backend"`
		Transports []struct {
			Type string `json:"type"`
			URL  string `json:"url"`
			Dial string `json:"dial"`
		} `json:"transports"`
	}
	if json.Unmarshal([]byte(config), &c) != nil || c.Backend != "openflux" {
		return nil
	}
	carriers := make(map[string]bool, len(c.Transports))
	for _, t := range c.Transports {
		address := t.URL
		if t.Type == "direct" {
			address = t.Dial
		}
		carriers[t.Type+" "+strings.TrimSpace(address)] = true
	}
	return carriers
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
		session := &extraSession{transport: isTunnelHelper(spec.GetPath()), tag: spec.GetOutboundTag(), config: spec.GetConfig()}
		if session.transport {
			session.backend = transportBackend(spec.GetConfig())
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
		path, noOut := spec.GetPath(), spec.GetNoOut()
		session.launch = func() (*process.Process, *transportHealth, error) {
			return launchExtraProcess(session, path, args, noOut)
		}
		extraSessions = append(extraSessions, session)
		child, health, e := session.launch()
		if e != nil {
			return e
		}
		session.child, session.health = child, health
		if session.transport {
			if e := awaitTransport(ctx, spec.GetConfig(), child, health); e != nil {
				return fmt.Errorf("transport startup failed: %w", e)
			}
		}
	}
	// Every transport is up: from now on its watchdog reconnects it like the original apps do.
	for _, session := range extraSessions {
		if session.transport && session.child != nil {
			session.stopping = make(chan struct{})
			session.watchDone = make(chan struct{})
			go session.watch()
		}
	}
	return nil
}

func launchExtraProcess(session *extraSession, path string, template []string, noOut bool) (*process.Process, *transportHealth, error) {
	config := session.config
	if session.backend == "csqtt" {
		// Stream counts and panel restarts for the watchdog; no addresses or hashes.
		var c map[string]json.RawMessage
		if json.Unmarshal([]byte(config), &c) == nil && c != nil {
			c["diagnostics"] = json.RawMessage("true")
			if encoded, err := json.Marshal(c); err == nil {
				config = string(encoded)
			}
		}
	}
	confPath, folder, err := process.CreateExtraConfig(config)
	if err != nil {
		return nil, nil, errors.New("failed to create private extra process configuration")
	}
	args := append([]string(nil), template...)
	for idx, arg := range args {
		if strings.Contains(arg, "%s") {
			args[idx] = fmt.Sprintf(arg, confPath)
			break
		}
	}
	child := process.NewProcess(path, args, noOut)
	var health *transportHealth
	if session.transport {
		if session.tag != "proxy" {
			child.SetRoutedTransport()
		}
		child.EnableStdinShutdown("STOP")
		child.SetSupervised()
		health = newTransportHealth(session.backend == "", nil)
		child.SetOutputObserver(func(b []byte) { health.accept(string(b)) })
	}
	child.SetCleanupPath(folder)
	if err := child.Start(); err != nil {
		return nil, nil, err
	}
	return child, health, nil
}

var errWrongTransportPassword = errors.New(wrongTransportPassword)

// awaitTransport waits for the helper's listener, and stops early on the original
// apps' wrong-password verdict instead of sitting out the whole authorization budget.
func awaitTransport(ctx context.Context, config string, child *process.Process, health *transportHealth) error {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	result := make(chan error, 1)
	go func() { result <- waitTransportReady(ctx, config, child.Done()) }()
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	for {
		select {
		case err := <-result:
			return err
		case <-ticker.C:
			if health != nil && health.wrongPassword() {
				return errWrongTransportPassword
			}
		}
	}
}

const (
	wrongTransportPassword = "Неверный пароль подключения или несовместимый WRAP: сервер не отвечает на рукопожатие"
	maxTransportRestarts   = 6
	transportRestartWindow = 10 * time.Minute
)

// watch keeps a transport connected: an exited or stalled helper is started again with the
// same private configuration (CSQTT gets a new epoch on every launch). Only a wrong password
// or a helper that keeps failing ends the profile, as an unexpected exit did before.
func (s *extraSession) watch() {
	defer close(s.watchDone)
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	var restarts []time.Time
	for {
		s.mu.Lock()
		child, health := s.child, s.health
		s.mu.Unlock()
		reason := ""
		select {
		case <-s.stopping:
			return
		case <-child.Done():
			reason = "transport exited"
		case <-ticker.C:
			if health != nil && health.wrongPassword() {
				fmt.Println("[qThrone] " + wrongTransportPassword)
				s.giveUp(child)
				return
			}
			if health == nil || !health.needsRestart() {
				continue
			}
			reason = "transport lost its connection"
		}
		now := time.Now()
		for len(restarts) > 0 && now.Sub(restarts[0]) > transportRestartWindow {
			restarts = restarts[1:]
		}
		if len(restarts) >= maxTransportRestarts {
			fmt.Println("[qThrone] Transport keeps failing; giving up after", len(restarts), "reconnects")
			s.giveUp(child)
			return
		}
		restarts = append(restarts, now)
		fmt.Printf("[qThrone] %s; reconnecting %s\n", reason, s.tag)
		child.Stop()
		select {
		case <-s.stopping:
			return
		case <-time.After(time.Duration(len(restarts)) * 2 * time.Second):
		}
		s.mu.Lock()
		select {
		case <-s.stopping:
			s.mu.Unlock()
			return
		default:
		}
		next, nextHealth, err := s.launch()
		if err == nil {
			s.child, s.health = next, nextHealth
		}
		s.mu.Unlock()
		if err != nil {
			fmt.Println("[qThrone] Transport restart failed:", err)
			s.giveUp(child)
			return
		}
		ctx, cancel := context.WithCancel(context.Background())
		go func() {
			select {
			case <-s.stopping:
				cancel()
			case <-ctx.Done():
			}
		}()
		err = awaitTransport(ctx, s.config, next, nextHealth)
		cancel()
		if errors.Is(err, errWrongTransportPassword) {
			fmt.Println("[qThrone] " + wrongTransportPassword)
			s.giveUp(next)
			return
		}
		if err == nil {
			fmt.Println("[qThrone] Transport reconnected:", s.tag)
		} else {
			// A helper that never became ready is replaced on the next round.
			next.Stop()
		}
	}
}

// giveUp leaves the dead transport to the GUI, which stops a main profile as before.
func (s *extraSession) giveUp(child *process.Process) {
	child.Stop()
	select {
	case <-s.stopping:
	default:
		child.ReportUnexpectedExit()
	}
}
