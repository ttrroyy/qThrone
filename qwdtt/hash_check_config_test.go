package main

import (
	"bytes"
	"context"
	"flag"
	"os"
	"path/filepath"
	"testing"
)

func TestHashCheckConfigIsIndependent(t *testing.T) {
	originalFlags, originalBridge := flag.CommandLine, desktopBridgeMode
	defer func() { flag.CommandLine, desktopBridgeMode = originalFlags, originalBridge }()
	flag.CommandLine = flag.NewFlagSet("check", flag.ContinueOnError)
	flag.CommandLine.SetOutput(new(bytes.Buffer))
	for _, name := range []string{"check-hashes", "vk", "vk-auth", "captcha-mode", "vk-anon-path", "go-dns", "device-id"} {
		flag.String(name, "", "")
	}
	peer := flag.String("peer", "active-server", "")
	path := filepath.Join(t.TempDir(), "check.json")
	if err := os.WriteFile(path, []byte(`{"hashes":["abcdefghijklmnop"],"captcha_mode":"auto","vk_anon_path":"vkcalls"}`), 0600); err != nil {
		t.Fatal(err)
	}
	if err := applyHashCheckConfig(path); err != nil {
		t.Fatal(err)
	}
	if *peer != "active-server" || flag.Lookup("vk-auth").Value.String() != "anonymous" || flag.Lookup("check-hashes").Value.String() != "true" || !desktopBridgeMode {
		t.Fatal("hash check changed tunnel settings or did not use original anonymous check path")
	}
	if err := os.WriteFile(path, []byte(`{"hashes":["bad|hash"]}`), 0600); err != nil {
		t.Fatal(err)
	}
	if applyHashCheckConfig(path) == nil {
		t.Fatal("invalid hash accepted")
	}
}

func TestCancelledHashCheckDoesNotStartRequests(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	// Must return before GetCreds, which would otherwise attempt VK networking.
	runHashChecks(ctx, []string{"abcdefghijklmnop"})
}
