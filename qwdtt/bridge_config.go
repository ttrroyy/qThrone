package main

import (
	"context"
	"crypto/sha256"
	"encoding/json"
	"flag"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// Secrets travel in Throne's private, temporary extra-core config, never argv.
type bridgeConfig struct {
	ProbeOnly          bool     `json:"probe_only"`
	InteractiveCaptcha bool     `json:"interactive_captcha"`
	VKAnonPath         string   `json:"vk_anon_path"`
	CaptchaMode        string   `json:"captcha_mode"`
	Peer               string   `json:"peer"`
	Password           string   `json:"password"`
	Hashes             []string `json:"hashes"`
	Mode               string   `json:"mode"`
	Workers            int      `json:"workers"`
	DeviceID           string   `json:"device_id"`
	TurnTCP            bool     `json:"turn_tcp"`
	Obfs               string   `json:"obfs"`
	DNS                string   `json:"go_dns"`
	Listen             string   `json:"listen"`
	SOCKS              string   `json:"socks"`
	SOCKSUser          string   `json:"socks_user"`
	SOCKSPass          string   `json:"socks_pass"`
}

var desktopBridgeMode bool
var desktopProbeOnly bool
var desktopInteractiveProbe bool
var bridgeContext context.Context
var bridgeCancel context.CancelFunc

func readBridgeConfig(filename string) (*bridgeConfig, error) {
	b, err := os.ReadFile(filename)
	if err != nil {
		return nil, err
	}
	var c bridgeConfig
	if err = json.Unmarshal(b, &c); err != nil {
		return nil, err
	}
	if c.Mode == "" {
		c.Mode = "raw"
	}
	if c.Mode != "raw" && c.Mode != "wg" {
		return nil, fmt.Errorf("mode must be raw or wg")
	}
	if c.Password == "" || strings.ContainsAny(c.Password, "|\r\n") {
		return nil, fmt.Errorf("invalid connection password")
	}
	host, port, err := net.SplitHostPort(c.Peer)
	p, _ := strconv.Atoi(port)
	if err != nil || host == "" || p < 1 || p > 65535 {
		return nil, fmt.Errorf("invalid server address")
	}
	if len(c.Hashes) == 0 || len(c.Hashes) > 4 {
		return nil, fmt.Errorf("one to four VK hashes required")
	}
	for _, h := range c.Hashes {
		if strings.TrimSpace(h) == "" {
			return nil, fmt.Errorf("empty VK hash")
		}
	}
	if c.Workers == 0 {
		c.Workers = 9
	}
	if c.Workers < 1 || c.Workers > 108 {
		return nil, fmt.Errorf("workers must be between 1 and 108")
	}
	// Match the profile editor and the anonymous client's groups of nine.
	c.Workers = max(9, min(c.Workers, len(c.Hashes)*27)) / 9 * 9
	if c.VKAnonPath == "" {
		c.VKAnonPath = "vkcalls"
	}
	if c.VKAnonPath != "vkcalls" && c.VKAnonPath != "legacy" {
		return nil, fmt.Errorf("invalid VK authorization mode")
	}
	if c.CaptchaMode == "" {
		c.CaptchaMode = "auto"
	}
	if c.CaptchaMode != "auto" && c.CaptchaMode != "wv" && c.CaptchaMode != "rjs" {
		return nil, fmt.Errorf("invalid captcha mode")
	}
	if c.Obfs == "" {
		c.Obfs = "audio"
	}
	if c.Obfs != "audio" && c.Obfs != "video" {
		return nil, fmt.Errorf("invalid obfuscation mode")
	}
	if c.DNS == "" {
		c.DNS = "yandex"
	}
	if c.DeviceID == "" || strings.ContainsAny(c.DeviceID, "|\r\n") {
		return nil, fmt.Errorf("invalid device ID")
	}
	for _, address := range []string{c.Listen, c.SOCKS} {
		h, p, e := net.SplitHostPort(address)
		n, _ := strconv.Atoi(p)
		if e != nil || h != "127.0.0.1" || n < 1 || n > 65535 {
			return nil, fmt.Errorf("bridge must listen on IPv4 loopback")
		}
	}
	if len(c.SOCKSUser) < 1 || len(c.SOCKSUser) > 255 || len(c.SOCKSPass) < 1 || len(c.SOCKSPass) > 255 {
		return nil, fmt.Errorf("invalid SOCKS bridge credentials")
	}
	return &c, nil
}

func applyBridgeConfig(filename string) error {
	full, err := filepath.Abs(filename)
	if err != nil {
		return err
	}
	c, err := readBridgeConfig(full)
	if err != nil {
		return err
	}
	desktopBridgeMode = true
	desktopProbeOnly = c.ProbeOnly
	desktopInteractiveProbe = c.ProbeOnly && c.InteractiveCaptcha
	mode := "socks"
	if c.Mode == "raw" {
		mode = "rawtun"
	}
	values := map[string]string{
		"peer": c.Peer, "password": c.Password, "vk": strings.Join(c.Hashes, ","),
		"mode": mode, "n": strconv.Itoa(c.Workers), "device-id": c.DeviceID,
		"turn-tcp": strconv.FormatBool(c.TurnTCP), "obfs": c.Obfs, "go-dns": c.DNS,
		"listen": c.Listen, "socks": c.SOCKS, "socks-auth": "true",
		"socks-user": c.SOCKSUser, "socks-pass": c.SOCKSPass,
		"captcha-mode": c.CaptchaMode, "vk-auth": "anonymous", "vk-anon-path": c.VKAnonPath,
	}
	for k, v := range values {
		if err := flag.Set(k, v); err != nil {
			return err
		}
	}
	// The privileged core owns the config directory on Unix. Keep qWDTT's
	// browser fingerprint files in an unprivileged, private per-device cache.
	cache, err := os.UserCacheDir()
	if err != nil {
		return err
	}
	key := sha256.Sum256([]byte(c.DeviceID))
	state := filepath.Join(cache, "qThrone", "qwdtt", fmt.Sprintf("%x", key[:16]))
	if err = os.MkdirAll(state, 0700); err != nil {
		return err
	}
	return os.Chdir(state)
}
