package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
	"wg-turn-client/internal/csqtt"
)

func TestDesktopCSQTTLegacyNegotiatedConfiguration(t *testing.T) {
	legacy := "TUNCONF:10.66.67.2:1.1.1.1:9000:stream-v1"
	for _, suffix := range []string{"", ":workers=5", ":workers=100"} {
		config, err := csqtt.ParseTunnelConfig(legacy + suffix)
		if err != nil {
			t.Fatal(err)
		}
		if config.Address.String() != "10.66.67.2" || config.Revision != "stream-v1" {
			t.Fatalf("unexpected tunnel config: %+v", config)
		}
	}
	for _, suffix := range []string{":workers=no", ":workers=101", ":workers=-1", ":unexpected=5", ":workers=5:extra"} {
		if _, err := csqtt.ParseTunnelConfig(legacy + suffix); err == nil {
			t.Fatalf("accepted invalid config %s", suffix)
		}
	}
}

func TestDesktopCSQTTLegacyModes(t *testing.T) {
	for _, workers := range []int{5, 10, 18, 100, 126} {
		document := map[string]any{"peer": "192.0.2.1:46000", "password": "secret", "device_id": "test-device",
			"listen": "127.0.0.1:9000", "socks": "127.0.0.1:10808", "socks_user": "private-user", "socks_pass": "private-pass",
			"hash_source": "auto_js", "vk_access_token": "vk1.private", "hashes": []string{}, "workers": workers,
			"obfs": "unified", "vk_anon_path": "auto_js", "captcha_mode": "rjs"}
		path := filepath.Join(t.TempDir(), "config.json")
		data, err := json.Marshal(document)
		if err != nil {
			t.Fatal(err)
		}
		if err = os.WriteFile(path, data, 0600); err != nil {
			t.Fatal(err)
		}
		config, err := readCSQTTConfig(path)
		if err != nil {
			t.Fatal(err)
		}
		want := 5
		if workers > 5 {
			want = max(5, min(workers, 100)/10*10)
		}
		if config.Workers != want {
			t.Fatalf("workers=%d: got %d, want %d", workers, config.Workers, want)
		}
		delete(document, "vk_access_token")
		data, _ = json.Marshal(document)
		if err = os.WriteFile(path, data, 0600); err != nil {
			t.Fatal(err)
		}
		if _, err = readCSQTTConfig(path); err == nil {
			t.Fatal("missing token and hashes accepted")
		}
		document["hashes"] = []string{"manual_hash_12345678"}
		document["hash_source"] = "manual"
		document["vk_anon_path"] = "vkcalls"
		data, _ = json.Marshal(document)
		if err = os.WriteFile(path, data, 0600); err != nil {
			t.Fatal(err)
		}
		if _, err = readCSQTTConfig(path); err != nil {
			t.Fatalf("legacy profile rejected: %v", err)
		}
	}
}
