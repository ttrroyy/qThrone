package main

import (
	"bufio"
	"context"
	"crypto/sha256"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"wg-turn-client/internal/csqtt"
)

func readCSQTTConfig(path string) (*bridgeConfig, error) {
	bytes, err := os.ReadFile(path)
	if err != nil {
		return nil, errors.New("cannot read private CSQTT configuration")
	}
	var c bridgeConfig
	if json.Unmarshal(bytes, &c) != nil {
		return nil, errors.New("invalid CSQTT configuration")
	}
	host, port, err := net.SplitHostPort(c.Peer)
	p, _ := strconv.Atoi(port)
	if err != nil || host == "" || p < 1 || p > 65535 {
		return nil, errors.New("invalid CSQTT peer")
	}
	if c.Password == "" || strings.ContainsAny(c.Password+c.DeviceID, "|\r\n") || c.DeviceID == "" {
		return nil, errors.New("invalid CSQTT credentials")
	}
	if len(c.Hashes) < 1 || len(c.Hashes) > 6 {
		return nil, errors.New("CSQTT needs one to six VK hashes")
	}
	for _, hash := range c.Hashes {
		if len(hash) < 16 || strings.ContainsAny(hash, ",|\r\n ") {
			return nil, errors.New("invalid CSQTT hash")
		}
	}
	c.Workers = csqtt.NormalizeWorkers(c.Workers, len(c.Hashes), false)
	for _, address := range []string{c.Listen, c.SOCKS} {
		host, port, err := net.SplitHostPort(address)
		p, _ := strconv.Atoi(port)
		if err != nil || host != "127.0.0.1" || p < 1 || p > 65535 {
			return nil, errors.New("invalid CSQTT loopback listener")
		}
	}
	if c.SOCKSUser == "" || c.SOCKSPass == "" {
		return nil, errors.New("CSQTT SOCKS credentials required")
	}
	if c.Obfs != "audio" && c.Obfs != "video" {
		return nil, errors.New("invalid CSQTT obfuscation")
	}
	if c.VKAnonPath != "vkcalls" && c.VKAnonPath != "legacy" {
		return nil, errors.New("invalid CSQTT authorization")
	}
	if c.CaptchaMode != "auto" && c.CaptchaMode != "wv" && c.CaptchaMode != "rjs" {
		return nil, errors.New("invalid CSQTT captcha mode")
	}
	return &c, nil
}

func runCSQTTBridge(path string) error {
	c, err := readCSQTTConfig(path)
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
				cancel()
				return
			}
		}
	}()
	path, err = filepath.Abs(path)
	if err != nil {
		return err
	}
	exe, err := os.Executable()
	if err != nil {
		return err
	}
	name := "csqtt-transport"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	command := exec.Command(filepath.Join(filepath.Dir(exe), name))
	command.Env = append(os.Environ(), "CSQTT_EVENTS=1", "CSQTT_CONFIG_FILE="+path)
	cache, err := os.UserCacheDir()
	if err != nil {
		return err
	}
	key := sha256.Sum256([]byte(c.DeviceID))
	command.Dir = filepath.Join(cache, "qThrone", "csqtt", fmt.Sprintf("%x", key[:16]))
	if err := os.MkdirAll(command.Dir, 0700); err != nil {
		return err
	}
	input, err := command.StdinPipe()
	if err != nil {
		return err
	}
	reader, writer := io.Pipe()
	command.Stdout, command.Stderr = writer, writer
	defer reader.Close()
	defer writer.Close()
	var inputMu sync.Mutex
	send := func(line string) { inputMu.Lock(); defer inputMu.Unlock(); _, _ = io.WriteString(input, line+"\n") }
	configs := make(chan string, 1)
	scanDone := make(chan struct{})
	go func() { defer close(scanDone); scanCSQTTOutput(ctx, c, reader, configs, send, cancel) }()
	defer func() { cancel(); reader.Close(); <-scanDone }()
	if err = command.Start(); err != nil {
		return errors.New("cannot start bundled CSQTT transport")
	}
	closeChildOwner, ownerErr := ownCSQTTChild(command)
	if ownerErr != nil {
		command.Process.Kill()
		command.Wait()
		return errors.New("cannot establish CSQTT transport lifetime ownership")
	}
	defer closeChildOwner()
	done := make(chan struct{})
	go func() { command.Wait(); writer.Close(); close(done) }()
	defer func() {
		cancel()
		send("STOP")
		select {
		case <-done:
		// The original client allows up to 9s and 8s for VK call leave tasks.
		// Do not truncate that cleanup and leave a stale call after every probe.
		case <-time.After(20 * time.Second):
			command.Process.Kill()
			<-done
		}
		input.Close()
	}()
	var config string
	timer := time.NewTimer(3 * time.Minute)
	defer timer.Stop()
	select {
	case config = <-configs:
	case <-done:
		return errors.New("CSQTT transport stopped before tunnel was ready")
	case <-ctx.Done():
		return nil
	case <-timer.C:
		return errors.New("CSQTT connection timed out")
	}
	tunnel, err := csqtt.ParseTunnelConfig(config)
	if err != nil {
		return err
	}
	// MTU is fixed at 1300 in the original Android VPN service.
	dns := make([]string, len(tunnel.DNS))
	for i, a := range tunnel.DNS {
		dns[i] = a.String()
	}
	device, network, err := startRawNetstack("RAWCONF:" + tunnel.Address.String() + "|" + strings.Join(dns, ",") + "|1300")
	if err != nil {
		return err
	}
	address, err := net.ResolveUDPAddr("udp4", c.Listen)
	if err != nil {
		device.Close()
		return err
	}
	socket, err := net.DialUDP("udp4", nil, address)
	if err != nil {
		device.Close()
		return err
	}
	relay := make(chan struct{})
	go func() { csqtt.RelayIP(ctx, device, socket); close(relay); cancel() }()
	defer func() { cancel(); <-relay }()
	listener, err := net.Listen("tcp4", c.SOCKS)
	if err != nil {
		return err
	}
	defer listener.Close()
	go func() {
		select {
		case <-done:
		case <-ctx.Done():
		}
		cancel()
		listener.Close()
	}()
	fmt.Println("[CSQTT] SOCKS bridge ready")
	err = serveBridgeSOCKS(ctx, listener, csqttTunnelDial(network.DialContext, dns), c.SOCKSUser, c.SOCKSPass)
	if ctx.Err() != nil {
		return nil
	}
	return err
}

func scanCSQTTOutput(ctx context.Context, c *bridgeConfig, reader io.Reader, configs chan<- string, send func(string), abort ...context.CancelFunc) {
	cancelSession := func() {
		if len(abort) > 0 {
			abort[0]()
		}
	}
	var solvers sync.WaitGroup
	defer solvers.Wait()
	var pendingConfig string
	workerReady, configSent := false, false
	forwardReadyConfig := func() {
		if !workerReady || pendingConfig == "" || configSent || ctx.Err() != nil {
			return
		}
		select {
		case configs <- pendingConfig:
			configSent = true
		default:
		}
	}
	scanner := bufio.NewScanner(reader)
	scanner.Buffer(make([]byte, 4096), 65536)
	for scanner.Scan() {
		line := scanner.Text()
		if i := strings.Index(line, "__CSQTT_EVENT__|READY|"); i >= 0 {
			var event struct {
				Worker *int `json:"worker"`
			}
			if json.Unmarshal([]byte(line[i+len("__CSQTT_EVENT__|READY|"):]), &event) == nil && event.Worker != nil {
				workerReady = true
				forwardReadyConfig()
			}
		}
		if os.Getenv("CSQTT_DIAGNOSTICS") == "1" {
			for _, kind := range []string{"STATS", "READY", "ACTIVE_ZERO", "NETWORK_SUSPECT", "SERVER_RESTART", "STOPPED"} {
				prefix := "__CSQTT_EVENT__|" + kind + "|"
				if i := strings.Index(line, prefix); i >= 0 {
					if kind == "STATS" {
						var stats struct {
							Active int   `json:"active"`
							Up     int64 `json:"bytes_up"`
							Down   int64 `json:"bytes_down"`
						}
						if json.Unmarshal([]byte(line[i+len(prefix):]), &stats) == nil {
							fmt.Printf("[CSQTT diagnostic] active=%d up=%d down=%d\n", stats.Active, stats.Up, stats.Down)
						}
					} else {
						fmt.Println("[CSQTT diagnostic] " + kind)
					}
				}
			}
		}
		if strings.Contains(line, "FATAL_AUTH:") || strings.Contains(line, "FATAL_PROTOCOL:") {
			message := "Сервер отказал в авторизации CSQTT. Проверьте пароль и его привязку в панели."
			if strings.Contains(line, "пароль привязан к другому устройству") {
				message = "Пароль CSQTT привязан к другому устройству. Создайте отдельный пароль для этого ПК."
			} else if strings.Contains(line, "FATAL_PROTOCOL:") {
				message = "Клиент и сервер CSQTT несовместимы по версии протокола или числу потоков."
			}
			fmt.Println("[CSQTT] " + message)
			cancelSession()
			return
		}
		if i := strings.Index(line, "__CSQTT_EVENT__|CONFIG|"); i >= 0 {
			var event struct {
				Config string `json:"config"`
			}
			if json.Unmarshal([]byte(line[i+len("__CSQTT_EVENT__|CONFIG|"):]), &event) == nil {
				pendingConfig = event.Config
				forwardReadyConfig()
			}
		}
		if i := strings.Index(line, "CAPTCHA_SOLVE|"); i >= 0 {
			parts := strings.SplitN(line[i:], "|", 4)
			if len(parts) != 4 {
				continue
			}
			visible := parts[1] != "auto"
			fmt.Println("[CSQTT] CAPTCHA_REQUIRED: требуется капча.")
			if c.ProbeOnly && !c.InteractiveCaptcha {
				send("CAPTCHA_RESULT|error:cancelled")
				cancelSession()
				continue
			}
			solvers.Add(1)
			go func(redirect string, visible bool) {
				defer solvers.Done()
				solveCtx := ctx
				if !visible {
					var cancel context.CancelFunc
					solveCtx, cancel = context.WithTimeout(ctx, 9*time.Second)
					defer cancel()
				}
				token, err := desktopCaptchaSolver(solveCtx, redirect, visible)
				if err != nil {
					token = "error:cancelled"
					if visible {
						cancelSession()
					}
				}
				send("CAPTCHA_RESULT|" + token)
			}(parts[2], visible)
		}
		// Raw transport logs may contain hashes and captcha session tokens.
		// Only explicit public lifecycle messages are emitted by this adapter.
	}
}
