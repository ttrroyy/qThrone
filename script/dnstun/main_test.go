package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestReadConfigTakesTheServiceFromEitherKey(t *testing.T) {
	folder := t.TempDir()
	for name, body := range map[string]string{
		// The desktop core keeps "backend" for the transport kind.
		"desktop": `{"type":"dnstt","backend":"dnstt","tunnel_backend":{"type":"ssh","user":"tunnel","password":"secret"},"domain":"t.example.com.","socks":"127.0.0.1:1080","socks_user":"u","socks_pass":"p"}`,
		// Android passes the service as "backend".
		"android": `{"type":"dnstt","backend":{"type":"ssh","user":"tunnel","password":"secret"},"domain":"t.example.com","socks":"127.0.0.1:1080","socks_user":"u","socks_pass":"p"}`,
	} {
		path := filepath.Join(folder, name+".json")
		if err := os.WriteFile(path, []byte(body), 0o600); err != nil {
			t.Fatal(err)
		}
		c, err := readConfig(path)
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		if c.Backend.Type != "ssh" || c.Backend.User != "tunnel" || c.Domain != "t.example.com" {
			t.Fatalf("%s: read %+v", name, c)
		}
	}
}

func TestResolversPreferProfileThenSystem(t *testing.T) {
	c := config{Resolvers: []string{" 9.9.9.9:53 "}, SystemResolvers: []string{"1.0.0.1:53"}}
	if got := c.resolverList(); len(got) != 1 || got[0] != "9.9.9.9:53" {
		t.Fatalf("profile resolvers ignored: %v", got)
	}
	c.Resolvers = nil
	if got := c.resolverList(); len(got) != 1 || got[0] != "1.0.0.1:53" {
		t.Fatalf("network resolvers ignored: %v", got)
	}
}
