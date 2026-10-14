package relay

import (
	"bytes"
	"context"
	"crypto/ed25519"
	"crypto/rand"
	"encoding/json"
	"fmt"
	"github.com/pkg/sftp"
	"github.com/zalando/go-keyring"
	"golang.org/x/crypto/ssh"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

type roundTripFunc func(*http.Request) (*http.Response, error)

func TestFingerprintEventsPreserveShortSecretCharacters(t *testing.T) {
	var output bytes.Buffer
	p := &protocol{output: json.NewEncoder(&output), secrets: []string{"1"}}
	fingerprint := "SHA256:a1b1c1d1e1f1"
	for _, kind := range []string{"trust", "host-key", "admin-pin"} {
		p.emit(kind, fingerprint, nil)
	}
	p.emit("log", "password=1", nil)
	decoder := json.NewDecoder(&output)
	for i := 0; i < 4; i++ {
		var event Event
		if e := decoder.Decode(&event); e != nil {
			t.Fatal(e)
		}
		if i < 3 && event.Text != fingerprint {
			t.Fatal("Fingerprint was corrupted by log redaction")
		}
		if i == 3 && event.Text != "password=[скрыто]" {
			t.Fatal("Console secret was not redacted")
		}
	}
}

func TestUploadReplacesLongerSecret(t *testing.T) {
	local, remote := net.Pipe()
	defer local.Close()
	defer remote.Close()
	_ = local.SetDeadline(time.Now().Add(10 * time.Second))
	_ = remote.SetDeadline(time.Now().Add(10 * time.Second))
	server, e := sftp.NewServer(remote, sftp.WithServerWorkingDirectory(t.TempDir()))
	if e != nil {
		t.Fatal(e)
	}
	defer server.Close()
	go func() { _ = server.Serve() }()
	client, e := sftp.NewClientPipe(local, local)
	if e != nil {
		t.Fatal(e)
	}
	defer client.Close()
	for _, value := range []string{"previous-long-password-that-must-not-remain", "new"} {
		if e = uploadFile(client, "secret.txt", []byte(value)); e != nil {
			t.Fatal(e)
		}
	}
	file, e := client.Open("secret.txt")
	if e != nil {
		t.Fatal(e)
	}
	defer file.Close()
	data, e := io.ReadAll(file)
	if e != nil || string(data) != "new" {
		t.Fatalf("Secret was not replaced completely: %q, %v", data, e)
	}
}

func (f roundTripFunc) RoundTrip(r *http.Request) (*http.Response, error) { return f(r) }
func TestVKHashesCacheAndRateLimit(t *testing.T) {
	keyring.MockInit()
	dir := t.TempDir()
	t.Setenv("APPDATA", dir)
	t.Setenv("XDG_CONFIG_HOME", dir)
	t.Setenv("HOME", dir)
	original := http.DefaultTransport
	defer func() { http.DefaultTransport = original }()
	calls := 0
	limited := false
	http.DefaultTransport = roundTripFunc(func(r *http.Request) (*http.Response, error) {
		if r.URL.Host != "api.vk.ru" || r.Method != "POST" || r.ParseForm() != nil || r.Form.Get("access_token") != "test-token" {
			t.Fatal("Incorrect VK API request")
		}
		calls++
		body := `{"response":{"join_link":"https://vk.ru/call/join/abcdefghijklmnop"}}`
		if limited {
			body = `{"error":{"error_code":14}}`
		}
		return &http.Response{StatusCode: 200, Body: io.NopCloser(strings.NewReader(body)), Header: make(http.Header)}, nil
	})
	if e := saveAccount(account{Token: "test-token", Expires: time.Now().Add(time.Hour).Unix()}); e != nil {
		t.Fatal(e)
	}
	request, _ := json.Marshal(Request{Operation: "vk-hashes", Count: 1})
	for i := 0; i < 2; i++ {
		var output bytes.Buffer
		if e := Run(bytes.NewReader(append(request, '\n')), &output); e != nil {
			t.Fatal(e)
		}
		if !strings.Contains(output.String(), "abcdefghijklmnop") || strings.Contains(output.String(), "test-token") {
			t.Fatal(output.String())
		}
	}
	if calls != 1 {
		t.Fatalf("Cached hashes generated again: %d", calls)
	}
	limited = true
	if e := saveAccount(account{Token: "test-token", Expires: time.Now().Add(time.Hour).Unix()}); e != nil {
		t.Fatal(e)
	}
	for i := 0; i < 2; i++ {
		var output bytes.Buffer
		if e := Run(bytes.NewReader(append(request, '\n')), &output); e == nil {
			t.Fatal("VK limit ignored")
		}
	}
	if calls != 2 {
		t.Fatal("Repeated request during VK cooldown")
	}
	unlink, _ := json.Marshal(Request{Operation: "secret-delete", ID: "account"})
	if e := Run(bytes.NewReader(append(unlink, '\n')), io.Discard); e != nil {
		t.Fatal(e)
	}
	info, e := accountInfo()
	if e != nil || info["linked"] != false {
		t.Fatal("Account remains linked")
	}
}
func TestVaultLockCancellation(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("APPDATA", dir)
	t.Setenv("XDG_CONFIG_HOME", dir)
	t.Setenv("HOME", dir)
	unlock, e := vaultLock(context.Background(), "account")
	if e != nil {
		t.Fatal(e)
	}
	defer unlock()
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, e = vaultLock(ctx, "account"); e == nil {
		t.Fatal("Cancelled waiter acquired lock")
	}
}

func TestVaultEncryption(t *testing.T) {
	keyring.MockInit()
	dir := t.TempDir()
	t.Setenv("APPDATA", dir)
	t.Setenv("XDG_CONFIG_HOME", dir)
	t.Setenv("HOME", dir)
	secret := strings.Repeat("secret-key-material\n", 400)
	if e := secureSet("servers", secret); e != nil {
		t.Fatal(e)
	}
	read, e := secureGet("servers")
	if e != nil || read != secret {
		t.Fatalf("Vault read: %v", e)
	}
	path, e := vaultPath("servers")
	if e != nil {
		t.Fatal(e)
	}
	data, e := os.ReadFile(path)
	if e != nil {
		t.Fatal(e)
	}
	if bytes.Contains(data, []byte("secret-key-material")) {
		t.Fatal("Plaintext stored")
	}
	data[len(data)-1] ^= 1
	if e = os.WriteFile(path, data, 0600); e != nil {
		t.Fatal(e)
	}
	if _, e = secureGet("servers"); e == nil {
		t.Fatal("Tampered vault accepted")
	}
	if e = secureDelete("servers"); e != nil {
		t.Fatal(e)
	}
	if _, e = secureGet("servers"); e != keyring.ErrNotFound {
		t.Fatal(e)
	}
}

func TestOAuthRedirect(t *testing.T) {
	valid := "https://oauth.vk.com/blank.html#" + url.Values{"access_token": {"test-token"}, "user_id": {"42"}, "state": {"state-123"}, "expires_in": {"3600"}}.Encode()
	a, ok := oauthToken(valid, "state-123")
	if !ok || a.User != "42" || a.Token != "test-token" || a.Expires <= time.Now().Unix() {
		t.Fatal("Valid VK redirect rejected")
	}
	for _, raw := range []string{strings.Replace(valid, "https:", "http:", 1), strings.Replace(valid, "oauth.vk.com", "oauth.vk.com.evil.example", 1), strings.Replace(valid, "/blank.html", "/other", 1), strings.Replace(valid, "state-123", "wrong", 1), strings.Replace(valid, "oauth.vk.com", "user@oauth.vk.com", 1)} {
		if _, ok := oauthToken(raw, "state-123"); ok {
			t.Fatalf("Untrusted redirect accepted: %s", raw)
		}
	}
	for _, raw := range []string{"https://vk.com/", "https://m.vk.ru/", "https://login.vk.com/?act=grant_access"} {
		if !trustedVK(raw) {
			t.Fatal(raw)
		}
	}
	for _, raw := range []string{"http://vk.com/", "https://vk.com.evil/", "https://vk.com:444/", "https://evil@vk.com/"} {
		if trustedVK(raw) {
			t.Fatal(raw)
		}
	}
}
func validServer() map[string]string {
	return map[string]string{"kind": "qwdtt", "host": "127.0.0.1", "user": "root", "auth": "password", "sshPassword": "secret", "sshPort": "22", "peerPort": "56000", "wgPort": "56001", "directPort": "56002", "rawPort": "56003", "adminPort": "56002", "password": "tunnel", "adminToken": "admin", "dns1": "1.1.1.1", "dns2": "1.0.0.1"}
}
func TestDeployInput(t *testing.T) {
	if e := validateServer(validServer(), true); e != nil {
		t.Fatal(e)
	}
	for key, value := range map[string]string{"kind": "other", "host": "127.0.0.1;id", "user": "root;id", "peerPort": "65536", "sshPort": "22;id", "dns1": "1.1.1.999", "password": "bad\nvalue", "adminToken": "bad\x00value"} {
		s := validServer()
		s[key] = value
		if validateServer(s, true) == nil {
			t.Fatalf("Accepted invalid %s", key)
		}
	}
	if quote("a'b") != "'a'\"'\"'b'" {
		t.Fatal("Shell quote")
	}
}
func TestSplitSecretsAreRedacted(t *testing.T) {
	var output, raw bytes.Buffer
	p := &protocol{output: json.NewEncoder(&output), secrets: []string{"private-secret"}}
	w := deployWriter{p: p, buffer: &raw}
	w.Write([]byte("value=private-"))
	w.Write([]byte("secret\n"))
	w.flush()
	if strings.Contains(output.String(), "private-") || !strings.Contains(output.String(), "[скрыто]") {
		t.Fatal(output.String())
	}
}

func TestHostVerificationBeforePassword(t *testing.T) {
	_, private, e := ed25519.GenerateKey(rand.Reader)
	if e != nil {
		t.Fatal(e)
	}
	signer, e := ssh.NewSignerFromKey(private)
	if e != nil {
		t.Fatal(e)
	}
	listener, e := net.Listen("tcp", "127.0.0.1:0")
	if e != nil {
		t.Fatal(e)
	}
	defer listener.Close()
	var authenticated atomic.Int32
	serverConfig := &ssh.ServerConfig{PasswordCallback: func(c ssh.ConnMetadata, p []byte) (*ssh.Permissions, error) {
		authenticated.Add(1)
		if string(p) != "secret" {
			return nil, fmt.Errorf("invalid password")
		}
		return nil, nil
	}}
	serverConfig.AddHostKey(signer)
	go func() {
		for {
			connection, e := listener.Accept()
			if e != nil {
				return
			}
			go func(connection net.Conn) {
				defer connection.Close()
				_ = connection.SetDeadline(time.Now().Add(10 * time.Second))
				server, channels, requests, e := ssh.NewServerConn(connection, serverConfig)
				if e != nil {
					return
				}
				defer server.Close()
				go ssh.DiscardRequests(requests)
				for channel := range channels {
					if channel.ChannelType() != "session" {
						channel.Reject(ssh.UnknownChannelType, "unsupported")
						continue
					}
					stream, requests, e := channel.Accept()
					if e != nil {
						return
					}
					for request := range requests {
						if request.Type == "exec" {
							request.Reply(true, nil)
							_, _ = io.WriteString(stream, "aarch64\n")
							_, _ = stream.SendRequest("exit-status", false, ssh.Marshal(struct{ Status uint32 }{0}))
							stream.Close()
							break
						}
						request.Reply(false, nil)
					}
				}
			}(connection)
		}
	}()
	host, port, _ := net.SplitHostPort(listener.Addr().String())
	for _, test := range []struct {
		name, key, reply string
		wantAuth         bool
	}{{"rejected", "", "reject", false}, {"changed", "SHA256:wrong", "", false}, {"approved", "", "approve", true}, {"pinned", ssh.FingerprintSHA256(signer.PublicKey()), "", true}} {
		t.Run(test.name, func(t *testing.T) {
			authenticated.Store(0)
			s := validServer()
			s["host"] = host
			s["sshPort"] = port
			s["hostKey"] = test.key
			request, _ := json.Marshal(Request{Operation: "deploy", Server: s})
			var output bytes.Buffer
			e := Run(strings.NewReader(string(request)+"\n"+test.reply+"\n"), &output)
			if e == nil {
				t.Fatal("Unsupported VPS architecture accepted")
			}
			if (authenticated.Load() > 0) != test.wantAuth {
				t.Fatalf("Password authentication before trust: %d", authenticated.Load())
			}
			if strings.Contains(output.String(), "secret") || strings.Contains(output.String(), "tunnel") {
				t.Fatal("Secret leaked to output")
			}
			if test.wantAuth && !strings.Contains(output.String(), "x86_64") {
				t.Fatalf("Unexpected result: %s", output.String())
			}
		})
	}
}
