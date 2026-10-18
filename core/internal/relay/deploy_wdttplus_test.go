package relay

import (
	"bytes"
	"crypto/ed25519"
	"crypto/rand"
	"encoding/json"
	"io"
	"net"
	"os"
	osexec "os/exec"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"golang.org/x/crypto/ssh"
)

func validWdttPlusServer() map[string]string {
	return map[string]string{"kind": "wdttplus", "host": "203.0.113.5", "user": "root", "auth": "password", "sshPassword": "ssh-secret",
		"sshPort": "22", "peerPort": "56000", "wgPort": "56001", "localPort": "9000", "password": "main-password", "dns": ""}
}

// fakeSSH answers exec requests with answer(command) and records every command it ran.
type fakeSSH struct {
	host, port, fingerprint string
	mu                      sync.Mutex
	commands                []string
}

func startFakeSSH(t *testing.T, answer func(string) (string, uint32)) *fakeSSH {
	t.Helper()
	_, private, _ := ed25519.GenerateKey(rand.Reader)
	signer, _ := ssh.NewSignerFromKey(private)
	config := &ssh.ServerConfig{PasswordCallback: func(ssh.ConnMetadata, []byte) (*ssh.Permissions, error) { return nil, nil }}
	config.AddHostKey(signer)
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { listener.Close() })
	f := &fakeSSH{fingerprint: ssh.FingerprintSHA256(signer.PublicKey())}
	f.host, f.port, _ = net.SplitHostPort(listener.Addr().String())
	go func() {
		for {
			connection, err := listener.Accept()
			if err != nil {
				return
			}
			go func(connection net.Conn) {
				defer connection.Close()
				_ = connection.SetDeadline(time.Now().Add(10 * time.Second))
				server, channels, requests, err := ssh.NewServerConn(connection, config)
				if err != nil {
					return
				}
				defer server.Close()
				go ssh.DiscardRequests(requests)
				for channel := range channels {
					stream, requests, err := channel.Accept()
					if err != nil {
						return
					}
					go func() {
						for request := range requests {
							if request.Type != "exec" {
								request.Reply(false, nil)
								continue
							}
							var payload struct{ Command string }
							ssh.Unmarshal(request.Payload, &payload)
							request.Reply(true, nil)
							f.mu.Lock()
							f.commands = append(f.commands, payload.Command)
							f.mu.Unlock()
							go io.Copy(io.Discard, stream)
							out, status := answer(payload.Command)
							io.WriteString(stream, out)
							stream.SendRequest("exit-status", false, ssh.Marshal(struct{ Status uint32 }{status}))
							stream.Close()
						}
					}()
				}
			}(connection)
		}
	}()
	return f
}

func TestWdttPlusDeployInput(t *testing.T) {
	if e := validateServer(validWdttPlusServer(), true); e != nil {
		t.Fatal(e)
	}
	for key, value := range map[string]string{"password": "short", "localPort": "0", "wgPort": "70000", "dns": "1.1.1.1;reboot"} {
		s := validWdttPlusServer()
		s[key] = value
		if validateServer(s, true) == nil {
			t.Fatalf("accepted invalid %s=%q", key, value)
		}
	}
	s := validWdttPlusServer()
	s["password"] = strings.Repeat("я", 256)
	s["dns"] = "1.1.1.1,2606:4700:4700::1111"
	if e := validateServer(s, true); e != nil {
		t.Fatalf("a 256-character password and a DNS list are valid: %v", e)
	}
}

func TestWdttPlusDeployCommandAndDatabase(t *testing.T) {
	s := validWdttPlusServer()
	fresh := wdttPlusCommand(s, "/tmp/wdtt-plus-deploy.AbC123", false)
	want := "env WDTT_DTLS_PORT=56000 WDTT_WG_PORT=56001 WDTT_SSH_PORT=22 WDTT_INSTALL_MODE=fresh WDTT_PRESERVE_DATA=0 " +
		"WDTT_STAGED_SERVER='/tmp/wdtt-plus-deploy.AbC123/server' WDTT_STAGED_DB='/tmp/wdtt-plus-deploy.AbC123/passwords.json' bash '/tmp/wdtt-plus-deploy.AbC123/deploy.sh'"
	if fresh != want {
		t.Fatalf("fresh install\n%s\nwant\n%s", fresh, want)
	}
	s["dns"] = "1.1.1.1,8.8.8.8"
	update := wdttPlusCommand(s, "/tmp/wdtt-plus-deploy.AbC123", true)
	if !strings.Contains(update, "WDTT_INSTALL_MODE=preserve WDTT_PRESERVE_DATA=1 WDTT_DNS='1.1.1.1,8.8.8.8'") || !strings.Contains(update, "WDTT_STAGED_DB=") {
		t.Fatalf("an update writes the kept database with the profile's settings: %s", update)
	}
	data, err := wdttPlusDatabase(nil, s)
	if err != nil {
		t.Fatal(err)
	}
	var db map[string]any
	if err := json.Unmarshal(data, &db); err != nil {
		t.Fatal(err)
	}
	if db["main_password"] != "main-password" || db["default_ports"] != "56000,56001,9000" || db["max_passwords"] != float64(50) || db["dns"] != "1.1.1.1,8.8.8.8" {
		t.Fatalf("passwords.json %v", db)
	}
	if !wdttPlusSuccess.MatchString("progress\nWDTT_INSTALL_RESULT=success\n") || wdttPlusSuccess.MatchString("echo WDTT_INSTALL_RESULT=success_not") {
		t.Fatal("success marker")
	}
}

func TestWdttPlusDeployLink(t *testing.T) {
	s := validWdttPlusServer()
	s["password"] = "p@ss word+1"
	if got := wdttPlusLink(s); got != "wdtt://connect?v=1&host=203.0.113.5&dtls=56000&wg=56001&local=9000&password=p%40ss%20word%2B1&hashes=&name=WDTT%20Plus" {
		t.Fatalf("WDTT Plus link %s", got)
	}
}

func TestWdttPlusDeployRefusesOtherArchitectures(t *testing.T) {
	f := startFakeSSH(t, func(command string) (string, uint32) {
		if command == "uname -m" {
			return "aarch64\n", 0
		}
		return "", 1
	})
	s := validWdttPlusServer()
	s["host"], s["sshPort"], s["hostKey"] = f.host, f.port, f.fingerprint
	request, _ := json.Marshal(Request{Operation: "deploy", Server: s})
	var output bytes.Buffer
	err := Run(strings.NewReader(string(request)+"\n"), &output)
	if err == nil || !strings.Contains(output.String(), "x86_64") {
		t.Fatalf("aarch64 accepted: %v %s", err, output.String())
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	if len(f.commands) != 1 {
		t.Fatalf("ran more than the architecture check: %v", f.commands)
	}
}

func TestDialNeedsTheRememberedHostKey(t *testing.T) {
	f := startFakeSSH(t, func(string) (string, uint32) { return "ok\n", 0 })
	s := validWdttPlusServer()
	s["host"], s["sshPort"] = f.host, f.port
	p := &protocol{output: json.NewEncoder(io.Discard)}
	p.ctx = t.Context()
	if _, err := p.dial(s, false); err == nil {
		t.Fatal("SSH without a remembered host key")
	}
	s["hostKey"] = "SHA256:another"
	if _, err := p.dial(s, false); err == nil || !strings.Contains(err.Error(), "изменился") {
		t.Fatalf("a changed host key accepted: %v", err)
	}
	s["hostKey"] = f.fingerprint
	client, err := p.dial(s, false)
	if err != nil {
		t.Fatal(err)
	}
	defer client.Close()
	out, err := p.run(client, s, "true", 10*time.Second, false)
	if err != nil || out != "ok\n" {
		t.Fatalf("run %q %v", out, err)
	}
}

// wdttPlusRoot runs a server-side script against a fake root folder instead of /.
func wdttPlusRoot(t *testing.T, root, script string) string {
	t.Helper()
	bash, err := osexec.LookPath("bash")
	if err != nil {
		t.Skip("bash is not available")
	}
	root = filepath.ToSlash(root)
	script = strings.NewReplacer("/etc/", root+"/etc/", "/usr/local/bin/", root+"/usr/local/bin/").Replace(script)
	out, _ := osexec.Command(bash, "-c", script).CombinedOutput()
	return string(out)
}

func wdttPlusFiles(t *testing.T, root string, files ...string) {
	t.Helper()
	for _, name := range files {
		path := filepath.Join(root, name)
		if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, []byte("{}"), 0o700); err != nil {
			t.Fatal(err)
		}
	}
}
