package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestPrivateConfig(t *testing.T) {
	valid := profile{Server: "https://vpn.example.org:444/", Username: " user ", Password: "secret", SOCKS: "127.0.0.1:1080", SOCKSUser: "local", SOCKSPass: "private"}
	for _, tc := range []struct {
		name string
		edit func(*profile)
		ok   bool
	}{
		{"desktop", func(*profile) {}, true},
		{"public listener", func(p *profile) { p.SOCKS = "0.0.0.0:1080" }, false},
		{"missing local authentication", func(p *profile) { p.SOCKSPass = "" }, false},
		{"invalid listener port", func(p *profile) { p.SOCKS = "127.0.0.1:0" }, false},
		{"missing username", func(p *profile) { p.Username = " " }, false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			p := valid
			tc.edit(&p)
			data, _ := json.Marshal(p)
			path := filepath.Join(t.TempDir(), "config.json")
			if err := os.WriteFile(path, data, 0600); err != nil {
				t.Fatal(err)
			}
			got, err := readConfig(path)
			if (err == nil) != tc.ok {
				t.Fatalf("valid=%v, error=%v", tc.ok, err)
			}
			if tc.ok && (got.Server != "vpn.example.org" || got.ServerPort != 444 || got.Username != "user" || got.Password != "secret") {
				t.Fatal("desktop configuration was not preserved")
			}
		})
	}
	for _, data := range []string{"{", strings.Repeat(" ", 65537)} {
		path := filepath.Join(t.TempDir(), "invalid.json")
		if err := os.WriteFile(path, []byte(data), 0600); err != nil {
			t.Fatal(err)
		}
		if _, err := readConfig(path); err == nil {
			t.Fatal("invalid private configuration accepted")
		}
	}
}
