package main

import (
	"encoding/base64"
	"encoding/json"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

const testHash = "AbCdEfGhIjKlMnOpQrSt"

func validWdttPlusConfig() wdttPlusConfig {
	return wdttPlusConfig{Peer: "203.0.113.5:56000", Password: "secret-pass", Hashes: []string{testHash}, Workers: 18,
		CaptchaMode: "auto", DeviceID: "device-1", DeviceInfo: `{"name":"PC"}`, Listen: "127.0.0.1:40001",
		SOCKS: "127.0.0.1:40002", SOCKSUser: "u", SOCKSPass: "p"}
}

func writeWdttPlusConfig(t *testing.T, c wdttPlusConfig) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "c.json")
	data, _ := json.Marshal(c)
	if err := os.WriteFile(path, data, 0600); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestWdttPlusArgsMatchItsTunnelManager(t *testing.T) {
	c := validWdttPlusConfig()
	args := strings.Join(wdttPlusArgs(&c), " ")
	want := "-peer 203.0.113.5:56000 -startup-config-stdin=true -n 18 -listen 127.0.0.1:40001 -transport-experiment auto -mode socks5 " +
		"-socks-listen 127.0.0.1:40002 -socks-udp=true -socks-auth=true -fingerprint firefox -client-ids 6287487,8202606 " +
		"-vkcalls-preflight=true -turn-stream-first=false -connection-udp-backoff=0 -rt-masque=false " +
		`-device-id device-1 -device-info {"name":"PC"} -config-first-start=true -captcha-mode auto`
	if args != want {
		t.Fatalf("args\n%s\nwant\n%s", args, want)
	}
	if strings.Contains(args, "secret-pass") || strings.Contains(args, testHash) {
		t.Fatal("secrets must travel on stdin, not the command line")
	}
	c.Hashes = append(c.Hashes, "ZyXwVuTsRqPoNmLkJiHg")
	if !slices.Contains(wdttPlusArgs(&c), "-hash-fallback=true") {
		t.Fatal("several hashes need -hash-fallback=true")
	}
}

func TestWdttPlusStartConfigCarriesTheSecrets(t *testing.T) {
	line := wdttPlusStartLine([]string{testHash, "ZyXwVuTsRqPoNmLkJiHg"}, "pw", "user", "pass")
	if !strings.HasPrefix(line, "START_CONFIG|") || strings.ContainsAny(line, "\n=+/") {
		t.Fatalf("start line %q", line)
	}
	raw, err := base64.RawURLEncoding.DecodeString(strings.TrimPrefix(line, "START_CONFIG|"))
	if err != nil {
		t.Fatal(err)
	}
	var start map[string]string
	if err := json.Unmarshal(raw, &start); err != nil {
		t.Fatal(err)
	}
	want := map[string]string{"vk_hashes": testHash + ",ZyXwVuTsRqPoNmLkJiHg", "connection_password": "pw",
		"custom_vk_client_id": "", "custom_vk_client_secret": "", "socks_username": "user", "socks_password": "pass"}
	if len(start) != len(want) {
		t.Fatalf("start config %v", start)
	}
	for key, value := range want {
		if start[key] != value {
			t.Fatalf("%s = %q, want %q", key, start[key], value)
		}
	}
}

func TestWdttPlusConfigValidation(t *testing.T) {
	if _, err := readWdttPlusConfig(writeWdttPlusConfig(t, validWdttPlusConfig())); err != nil {
		t.Fatal(err)
	}
	for name, change := range map[string]func(*wdttPlusConfig){
		"SOCKS on all interfaces": func(c *wdttPlusConfig) { c.SOCKS = "0.0.0.0:40002" },
		"no SOCKS login":          func(c *wdttPlusConfig) { c.SOCKSPass = "" },
		"workers off the 9 grid":  func(c *wdttPlusConfig) { c.Workers = 20 },
		"too many workers":        func(c *wdttPlusConfig) { c.Workers = 117 },
		"five hashes": func(c *wdttPlusConfig) {
			c.Hashes = []string{testHash, testHash + "a", testHash + "b", testHash + "c", testHash + "d"}
		},
		"short hash":             func(c *wdttPlusConfig) { c.Hashes = []string{"short"} },
		"line break in password": func(c *wdttPlusConfig) { c.Password = "a\nb" },
		"unknown captcha mode":   func(c *wdttPlusConfig) { c.CaptchaMode = "manual" },
	} {
		c := validWdttPlusConfig()
		change(&c)
		if _, err := readWdttPlusConfig(writeWdttPlusConfig(t, c)); err == nil {
			t.Fatalf("%s accepted", name)
		}
	}
}

func TestWdttPlusCaptchaRequestsAndLog(t *testing.T) {
	id, visible, redirect, ok := wdttPlusCaptchaRequest("CAPTCHA_SOLVE|s1-2|auto|https://id.vk.ru/not_robot_captcha?x=1|token|tail")
	if !ok || id != "s1-2" || visible || redirect != "https://id.vk.ru/not_robot_captcha?x=1" {
		t.Fatalf("request read as %q %v %q %v", id, visible, redirect, ok)
	}
	if _, visible, _, ok := wdttPlusCaptchaRequest("log CAPTCHA_SOLVE|7|manual|https://vk.com/c|t"); !ok || !visible {
		t.Fatal("a manual request is a visible window")
	}
	for _, bad := range []string{"CAPTCHA_SOLVE|1|auto|https://evil.example/|t", "CAPTCHA_SOLVE|a b|auto|https://vk.com/c|t", "CAPTCHA_SOLVE|1|auto|https://vk.com/c"} {
		if _, _, _, ok := wdttPlusCaptchaRequest(bad); ok {
			t.Fatalf("%q accepted", bad)
		}
	}
	if wdttPlusLogLine("CAPTCHA_SOLVE|1|auto|https://vk.com/c|session-token") == "" || strings.Contains(wdttPlusLogLine("CAPTCHA_SOLVE|1|auto|https://vk.com/c|session-token"), "session-token") {
		t.Fatal("a captcha request is logged without its token")
	}
	if wdttPlusLogLine("[STREAM 1] joined vk.com/call/join/"+testHash) != "" {
		t.Fatal("raw client output must stay out of the log")
	}
}

func TestWdttPlusHashVerdictHidesTheHash(t *testing.T) {
	cases := map[string]string{
		"HASH_CHECK_START|1|" + testHash:                           "HASH_CHECK_START|1",
		"HASH_CHECK|2|" + testHash + "|ok|TURN urls=2":             "HASH_CHECK|2|ok",
		"HASH_CHECK|1|" + testHash + "|dead|call link " + testHash: "HASH_CHECK|1|dead",
		"HASH_CHECK|1|" + testHash + "|Weird Status|x":             "HASH_CHECK|1|error",
		"HASH_CHECK|3|" + testHash + "|ok|x":                       "",
		"HASH_CHECK|0|" + testHash + "|ok|x":                       "",
		"something else":                                           "",
	}
	for line, want := range cases {
		if got := wdttPlusHashVerdict(line, 2); got != want {
			t.Fatalf("%q -> %q, want %q", line, got, want)
		}
	}
}
