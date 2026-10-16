package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"strings"
)

// The desktop check uses the original -check-hashes path in a separate process.
// Its private configuration contains no tunnel/VPS connection parameters.
func applyHashCheckConfig(path string) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	var c struct {
		Hashes  []string `json:"hashes"`
		Captcha string   `json:"captcha_mode"`
		Path    string   `json:"vk_anon_path"`
		DNS     string   `json:"go_dns"`
		Device  string   `json:"device_id"`
	}
	if err = json.NewDecoder(io.LimitReader(f, 16384)).Decode(&c); err != nil {
		return err
	}
	if len(c.Hashes) < 1 || len(c.Hashes) > 4 {
		return fmt.Errorf("one to four VK hashes required")
	}
	for _, h := range c.Hashes {
		if len(h) < 8 || strings.ContainsAny(h, "/|, \r\n") {
			return fmt.Errorf("invalid VK hash")
		}
	}
	if c.Captcha == "" {
		c.Captcha = "auto"
	}
	if c.Path == "" {
		c.Path = "vkcalls"
	}
	if c.DNS == "" {
		c.DNS = "yandex"
	}
	if c.Captcha != "auto" && c.Captcha != "wv" && c.Captcha != "rjs" {
		return fmt.Errorf("invalid captcha mode")
	}
	if c.Path != "vkcalls" && c.Path != "legacy" {
		return fmt.Errorf("invalid VK path")
	}
	for k, v := range map[string]string{"check-hashes": "true", "vk": strings.Join(c.Hashes, ","), "vk-auth": "anonymous", "captcha-mode": c.Captcha, "vk-anon-path": c.Path, "go-dns": c.DNS, "device-id": c.Device} {
		if err = flag.Set(k, v); err != nil {
			return err
		}
	}
	// Browser, DNS and credential caches belong solely to this child process.
	desktopBridgeMode = true
	if c.Device != "" {
		return enterDesktopState(c.Device)
	}
	return nil
}
