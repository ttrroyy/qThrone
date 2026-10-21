package main

import (
	"bufio"
	"context"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"regexp"
	"runtime"
	"slices"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

// WDTT Plus runs its own native client (github.com/Ivan4537/WDTT-Plus go_client) as its
// app's TunnelManager starts it: flags on the command line, the hashes, password and the
// SOCKS login on stdin as one START_CONFIG line. The client serves the authenticated
// SOCKS5 itself; this bridge answers its numbered captcha requests and keeps its raw
// output, which carries hashes and captcha tokens, out of qThrone's log.
type wdttPlusConfig struct {
	TCPOnly            bool     `json:"tcp_only"`
	TurnSNI            string   `json:"turn_sni"`
	RTMasque           bool     `json:"rt_masque"`
	ServerBootstrap    bool     `json:"rt_masque_server_bootstrap"`
	Fingerprint        string   `json:"fingerprint"`
	ClientIDs          string   `json:"client_ids"`
	VKCallsPreflight   *bool    `json:"vkcalls_preflight"`
	Peer               string   `json:"peer"`
	Password           string   `json:"password"`
	Hashes             []string `json:"hashes"`
	Workers            int      `json:"workers"`
	CaptchaMode        string   `json:"captcha_mode"`
	DeviceID           string   `json:"device_id"`
	DeviceInfo         string   `json:"device_info"`
	Listen             string   `json:"listen"`
	SOCKS              string   `json:"socks"`
	SOCKSUser          string   `json:"socks_user"`
	SOCKSPass          string   `json:"socks_pass"`
	ProbeOnly          bool     `json:"probe_only"`
	LatencyProbe       bool     `json:"latency_probe"`
	InteractiveCaptcha bool     `json:"interactive_captcha"`
}

var vkHashPattern = regexp.MustCompile(`^[A-Za-z0-9_-]{16,512}$`)
var wdttPlusRequestID = regexp.MustCompile(`^[A-Za-z0-9_.:-]{1,128}$`)
var wdttPlusHashStatus = regexp.MustCompile(`^[a-z_]{1,32}$`)

func loopbackAddress(address string) bool {
	host, port, err := net.SplitHostPort(address)
	p, _ := strconv.Atoi(port)
	return err == nil && host == "127.0.0.1" && p >= 1 && p <= 65535
}

func readWdttPlusConfig(path string) (*wdttPlusConfig, error) {
	bytes, err := os.ReadFile(path)
	if err != nil {
		return nil, errors.New("cannot read private WDTT Plus configuration")
	}
	var c wdttPlusConfig
	if json.Unmarshal(bytes, &c) != nil {
		return nil, errors.New("invalid WDTT Plus configuration")
	}
	host, port, err := net.SplitHostPort(c.Peer)
	p, _ := strconv.Atoi(port)
	if err != nil || host == "" || p < 1 || p > 65535 {
		return nil, errors.New("invalid WDTT Plus peer")
	}
	if c.Password == "" || c.DeviceID == "" || strings.ContainsAny(c.Password+c.DeviceID+c.DeviceInfo, "\r\n") {
		return nil, errors.New("invalid WDTT Plus credentials")
	}
	if len(c.Hashes) < 1 || len(c.Hashes) > 4 {
		return nil, errors.New("WDTT Plus needs one to four VK hashes")
	}
	for _, hash := range c.Hashes {
		if !vkHashPattern.MatchString(hash) {
			return nil, errors.New("invalid WDTT Plus hash")
		}
	}
	if c.Workers < 9 || c.Workers > 108 || c.Workers%9 != 0 {
		return nil, errors.New("WDTT Plus workers go in groups of 9 up to 108")
	}
	if c.CaptchaMode != "auto" && c.CaptchaMode != "wv" && c.CaptchaMode != "rjs" {
		return nil, errors.New("invalid WDTT Plus captcha mode")
	}
	if !loopbackAddress(c.Listen) || !loopbackAddress(c.SOCKS) {
		return nil, errors.New("invalid WDTT Plus loopback listener")
	}
	if c.SOCKSUser == "" || c.SOCKSPass == "" {
		return nil, errors.New("WDTT Plus SOCKS credentials required")
	}
	if c.Fingerprint == "" {
		c.Fingerprint = "firefox"
	}
	if c.ClientIDs == "" {
		c.ClientIDs = "6287487,8202606"
	}
	if !regexp.MustCompile(`^[0-9]+(?:,[0-9]+){0,15}$`).MatchString(c.ClientIDs) {
		return nil, errors.New("invalid WDTT Plus client IDs")
	}
	if !slices.Contains([]string{"firefox", "chrome", "safari", "ios", "android", "random"}, c.Fingerprint) || !wdttPlusValidSNI(c.TurnSNI) {
		return nil, errors.New("invalid WDTT Plus TLS profile")
	}
	return &c, nil
}

// wdttPlusArgs are the flags of WDTT Plus's TunnelManager; Go boolean flags only as -flag=value.
func wdttPlusArgs(c *wdttPlusConfig) []string {
	fingerprint, ids := c.Fingerprint, c.ClientIDs
	if fingerprint == "" {
		fingerprint = "firefox"
	}
	if ids == "" {
		ids = "6287487,8202606"
	}
	preflight := c.VKCallsPreflight == nil || *c.VKCallsPreflight
	args := []string{
		"-peer", c.Peer, "-startup-config-stdin=true",
		"-n", strconv.Itoa(c.Workers), "-listen", c.Listen,
		"-transport-experiment", "auto", "-mode", "socks5",
		"-socks-listen", c.SOCKS, "-socks-udp=true", "-socks-auth=true",
		"-fingerprint", fingerprint, "-client-ids", ids,
		"-vkcalls-preflight=" + strconv.FormatBool(preflight), "-turn-stream-first=" + strconv.FormatBool(c.TCPOnly), "-connection-udp-backoff=0", "-rt-masque=" + strconv.FormatBool(c.RTMasque),
		"-device-id", c.DeviceID, "-device-info", c.DeviceInfo,
		"-config-first-start=true",
	}
	if c.TurnSNI != "" {
		args = append(args, "-turn-sni", c.TurnSNI)
	}
	if len(c.Hashes) > 1 {
		args = append(args, "-hash-fallback=true")
	}
	return append(args, "-captcha-mode", c.CaptchaMode)
}

func wdttPlusValidSNI(host string) bool {
	if host == "" {
		return true
	}
	if len(host) > 253 || !strings.Contains(host, ".") || net.ParseIP(host) != nil {
		return false
	}
	label := regexp.MustCompile(`^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$`)
	for _, part := range strings.Split(host, ".") {
		if !label.MatchString(part) {
			return false
		}
	}
	return true
}

// wdttPlusStartLine is START_CONFIG|<base64url JSON>, as the app hands its secrets over.
func wdttPlusStartLine(hashes []string, password, socksUser, socksPass string) string {
	start := map[string]string{
		"vk_hashes": strings.Join(hashes, ","), "connection_password": password,
		"custom_vk_client_id": "", "custom_vk_client_secret": "",
	}
	if socksUser != "" {
		start["socks_username"], start["socks_password"] = socksUser, socksPass
	}
	encoded, _ := json.Marshal(start)
	return "START_CONFIG|" + base64.RawURLEncoding.EncodeToString(encoded)
}

// wdttPlusCaptchaRequest reads CAPTCHA_SOLVE|id|mode|uri|token.
func wdttPlusCaptchaRequest(line string) (id string, visible bool, redirect string, ok bool) {
	i := strings.Index(line, "CAPTCHA_SOLVE|")
	if i < 0 {
		return "", false, "", false
	}
	parts := strings.SplitN(line[i:], "|", 5)
	if len(parts) != 5 || !wdttPlusRequestID.MatchString(parts[1]) || len(parts[3]) > 8192 || !vkCaptchaURL(parts[3]) {
		return "", false, "", false
	}
	return parts[1], parts[2] != "auto", parts[3], true
}

func wdttPlusExecutable() (string, error) {
	exe, err := os.Executable()
	if err != nil {
		return "", err
	}
	name := "wdttplus-client"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	path := filepath.Join(filepath.Dir(exe), name)
	if _, err := os.Stat(path); err != nil {
		return "", errors.New("bundled WDTT Plus client is missing")
	}
	return path, nil
}

// wdttPlusChild runs the client with its private state directory and a serialized stdin.
type wdttPlusChild struct {
	command *exec.Cmd
	input   io.WriteCloser
	output  *io.PipeReader
	mu      sync.Mutex
	done    chan struct{}
}

func startWdttPlusChild(args []string, deviceID string, start string) (*wdttPlusChild, error) {
	path, err := wdttPlusExecutable()
	if err != nil {
		return nil, err
	}
	cache, err := os.UserCacheDir()
	if err != nil {
		return nil, err
	}
	key := sha256.Sum256([]byte(deviceID))
	command := exec.Command(path, args...)
	command.Dir = filepath.Join(cache, "qThrone", "wdttplus", fmt.Sprintf("%x", key[:16]))
	if err := os.MkdirAll(command.Dir, 0700); err != nil {
		return nil, err
	}
	input, err := command.StdinPipe()
	if err != nil {
		return nil, err
	}
	reader, writer := io.Pipe()
	command.Stdout, command.Stderr = writer, writer
	if err := command.Start(); err != nil {
		return nil, errors.New("cannot start bundled WDTT Plus client")
	}
	child := &wdttPlusChild{command: command, input: input, output: reader, done: make(chan struct{})}
	go func() { command.Wait(); writer.Close(); close(child.done) }()
	child.send(start)
	return child, nil
}

func (c *wdttPlusChild) send(line string) {
	c.mu.Lock()
	defer c.mu.Unlock()
	_, _ = io.WriteString(c.input, line+"\n")
}

// stop asks the client to leave its calls, as the app does, before it is killed.
func (c *wdttPlusChild) stop() {
	c.send("STOP")
	select {
	case <-c.done:
	case <-time.After(20 * time.Second):
		c.command.Process.Kill()
		<-c.done
	}
	c.input.Close()
	c.output.Close()
}

// answerWdttPlusCaptcha solves one numbered request inside the client's own wait (25 s
// hidden, 195 s visible) and answers CAPTCHA_RESULT|id|value.
func answerWdttPlusCaptcha(ctx context.Context, send func(string), id string, visible bool, redirect string) bool {
	budget := wdttPlusAutoTimeout + 6*time.Second
	if visible {
		budget = wdttPlusVisibleTimeout + 10*time.Second
	}
	solveCtx, cancel := context.WithTimeout(ctx, budget)
	defer cancel()
	token, err := wdttPlusCaptchaSolver(solveCtx, redirect, visible)
	if err != nil {
		token = "error:cancelled"
		if !visible {
			token = "error:timeout"
		}
	}
	send("CAPTCHA_RESULT|" + id + "|" + token)
	return err == nil
}

// wdttPlusLogLine keeps only the client's lifecycle words, never hashes, tokens or addresses.
func wdttPlusLogLine(line string) string {
	switch {
	case strings.Contains(line, "PROXY_READY|"):
		return "[WDTT Plus] SOCKS ready"
	case strings.Contains(line, "CAPTCHA_SOLVE|"):
		return "[WDTT Plus] CAPTCHA_REQUIRED: требуется капча."
	case strings.Contains(line, "PEER_DNS_READY|"):
		return "[WDTT Plus] server address resolved"
	}
	return ""
}

func runWdttPlusBridge(path string) error {
	c, err := readWdttPlusConfig(path)
	if err != nil {
		return err
	}
	ctx, cancel := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
	defer cancel()
	go func() {
		defer cancel()
		scanner := bufio.NewScanner(os.Stdin)
		for scanner.Scan() {
			if strings.TrimSpace(scanner.Text()) == "STOP" {
				return
			}
		}
	}()
	args := wdttPlusArgs(c)
	if c.RTMasque {
		cache, err := os.UserCacheDir()
		if err != nil {
			return err
		}
		key := sha256.Sum256([]byte(c.DeviceID))
		directory := filepath.Join(cache, "qThrone", "wdttplus", fmt.Sprintf("%x", key[:16]))
		if err := os.MkdirAll(directory, 0700); err != nil {
			return err
		}
		config := filepath.Join(directory, "rt-masque-v1.json")
		args = append(args, "-rt-masque-config", config, "-rt-masque-accept-tos=true")
		if _, err := os.Stat(config); os.IsNotExist(err) && c.ServerBootstrap {
			address, stop := startWdttPlusBootstrap(ctx, c.Peer)
			defer stop()
			if address != "" {
				args = append(args, "-warp-api-relay", address)
			}
		}
	}
	child, err := startWdttPlusChild(args, c.DeviceID, wdttPlusStartLine(c.Hashes, c.Password, c.SOCKSUser, c.SOCKSPass))
	if err != nil {
		return err
	}
	defer child.stop()
	var solvers sync.WaitGroup
	defer solvers.Wait()
	scanDone := make(chan struct{})
	go func() {
		defer close(scanDone)
		scanner := bufio.NewScanner(child.output)
		scanner.Buffer(make([]byte, 4096), 65536)
		for scanner.Scan() {
			line := scanner.Text()
			if message := wdttPlusLogLine(line); message != "" {
				fmt.Println(message)
			}
			id, visible, redirect, ok := wdttPlusCaptchaRequest(line)
			if !ok {
				continue
			}
			// A background ping answers hidden checks only: a window is the user's call.
			if visible && (c.ProbeOnly || c.LatencyProbe) && !c.InteractiveCaptcha {
				child.send("CAPTCHA_RESULT|" + id + "|error:cancelled")
				cancel()
				continue
			}
			solvers.Add(1)
			go func() {
				defer solvers.Done()
				if !answerWdttPlusCaptcha(ctx, child.send, id, visible, redirect) && visible {
					cancel()
				}
			}()
		}
	}()
	select {
	case <-ctx.Done():
		return nil
	case <-child.done:
		<-scanDone
		if ctx.Err() != nil {
			return nil
		}
		return errors.New("WDTT Plus client stopped")
	}
}

// runWdttPlusHashCheck is the app's check: -check-hashes, the hashes as START_CONFIG and
// the client's own captcha chain, 90 s per hash. Only "HASH_CHECK_START|n" and
// "HASH_CHECK|n|status" reach the caller; VK's messages may echo the call link.
func runWdttPlusHashCheck(path string) error {
	bytes, err := os.ReadFile(path)
	if err != nil {
		return errors.New("cannot read private hash check")
	}
	var c struct {
		Hashes      []string `json:"hashes"`
		CaptchaMode string   `json:"captcha_mode"`
	}
	if json.Unmarshal(bytes, &c) != nil || len(c.Hashes) < 1 || len(c.Hashes) > 4 {
		return errors.New("invalid hash check")
	}
	for _, hash := range c.Hashes {
		if !vkHashPattern.MatchString(hash) {
			return errors.New("invalid VK hash")
		}
	}
	if c.CaptchaMode != "wv" && c.CaptchaMode != "rjs" {
		c.CaptchaMode = "auto"
	}
	ctx, cancel := context.WithTimeout(context.Background(), time.Duration(len(c.Hashes))*100*time.Second)
	defer cancel()
	args := []string{"-check-hashes", "-startup-config-stdin=true", "-captcha-mode", c.CaptchaMode,
		"-fingerprint", "firefox", "-client-ids", "6287487,8202606", "-vkcalls-preflight=true"}
	child, err := startWdttPlusChild(args, "hash-check", wdttPlusStartLine(c.Hashes, "", "", ""))
	if err != nil {
		return err
	}
	defer child.stop()
	go func() { <-ctx.Done(); child.command.Process.Kill() }()
	var solvers sync.WaitGroup
	defer solvers.Wait()
	scanner := bufio.NewScanner(child.output)
	scanner.Buffer(make([]byte, 4096), 65536)
	for scanner.Scan() {
		line := scanner.Text()
		if verdict := wdttPlusHashVerdict(line, len(c.Hashes)); verdict != "" {
			fmt.Println(verdict)
		}
		if id, visible, redirect, ok := wdttPlusCaptchaRequest(line); ok {
			solvers.Add(1)
			go func() { defer solvers.Done(); answerWdttPlusCaptcha(ctx, child.send, id, visible, redirect) }()
		}
	}
	return nil
}

// wdttPlusHashVerdict turns HASH_CHECK_START|n|hash and HASH_CHECK|n|hash|status|message
// into the hash-free lines the profile editor reads.
func wdttPlusHashVerdict(line string, count int) string {
	start := strings.HasPrefix(line, "HASH_CHECK_START|")
	if !start && !strings.HasPrefix(line, "HASH_CHECK|") {
		return ""
	}
	limit := 5
	if start {
		limit = 3
	}
	parts := strings.SplitN(line, "|", limit)
	slot, err := strconv.Atoi(parts[min(1, len(parts)-1)])
	if len(parts) != limit || err != nil || slot < 1 || slot > count {
		return ""
	}
	if start {
		return fmt.Sprintf("HASH_CHECK_START|%d", slot)
	}
	status := parts[3]
	if !wdttPlusHashStatus.MatchString(status) {
		status = "error"
	}
	return fmt.Sprintf("HASH_CHECK|%d|%s", slot, status)
}
