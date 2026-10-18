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
	"runtime"
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
	fresh := wdttPlusCommand(s, "/tmp/wdtt-plus-deploy.AbC123", true, false)
	want := "env WDTT_DTLS_PORT=56000 WDTT_WG_PORT=56001 WDTT_SSH_PORT=22 WDTT_INSTALL_MODE=fresh WDTT_PRESERVE_DATA=0 " +
		"WDTT_STAGED_SERVER='/tmp/wdtt-plus-deploy.AbC123/server' WDTT_STAGED_DB='/tmp/wdtt-plus-deploy.AbC123/passwords.json' bash '/tmp/wdtt-plus-deploy.AbC123/deploy.sh'"
	if fresh != want {
		t.Fatalf("fresh install\n%s\nwant\n%s", fresh, want)
	}
	s["dns"] = "1.1.1.1,8.8.8.8"
	update := wdttPlusCommand(s, "/tmp/wdtt-plus-deploy.AbC123", true, true)
	if !strings.Contains(update, "WDTT_INSTALL_MODE=preserve WDTT_PRESERVE_DATA=1 WDTT_DNS='1.1.1.1,8.8.8.8'") || strings.Contains(update, "WDTT_STAGED_DB") {
		t.Fatalf("an update keeps the server's data: %s", update)
	}
	if got := wdttPlusCommand(s, "/tmp/wdtt-plus-deploy.AbC123", false, true); got != "bash '/tmp/wdtt-plus-deploy.AbC123/deploy.sh' uninstall" {
		t.Fatalf("uninstall %s", got)
	}
	var db map[string]any
	if err := json.Unmarshal(wdttPlusDatabase(s), &db); err != nil {
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

// Regression: after deploy.sh uninstall only /etc/wdtt is left, and the preserving update
// stopped with "признаки Android-установки изменились после проверки" until the data was marked.
func TestWdttPlusProbeTellsKeptDataFromInstalledService(t *testing.T) {
	data := []string{"etc/wdtt/passwords.json", "etc/wdtt/wg-keys.dat"}
	for _, c := range []struct {
		files []string
		want  string
	}{
		{nil, "WDTT_DB=0\nWDTT_SERVICE=none\n"},
		{data, "WDTT_DB=1\nWDTT_SERVICE=none\n"},
		{append(data, "etc/systemd/system/wdtt.service", "usr/local/bin/wdtt-server"), "WDTT_DB=1\nWDTT_SERVICE=installed\n"},
		{append(data, "etc/systemd/system/wdtt.service"), "WDTT_DB=1\nWDTT_SERVICE=partial\n"},
	} {
		root := t.TempDir()
		wdttPlusFiles(t, root, c.files...)
		if got := wdttPlusRoot(t, root, wdttPlusProbe); got != c.want {
			t.Fatalf("%v: %q, want %q", c.files, got, c.want)
		}
	}
}

func TestWdttPlusMarksKeptDataAsWdttPlusDoes(t *testing.T) {
	root := t.TempDir()
	wdttPlusFiles(t, root, "etc/wdtt/passwords.json", "etc/wdtt/wg-keys.dat")
	if out := wdttPlusRoot(t, root, wdttPlusMarkPreserved); !strings.Contains(out, "WDTT_PRESERVED_MARKER=ready") {
		t.Fatalf("marker not written: %s", out)
	}
	marker := filepath.Join(root, "etc/wdtt/.android-deploy-preserved")
	if data, err := os.ReadFile(marker); err != nil || string(data) != "Preserved by WDTT Plus Android deploy\n" {
		t.Fatalf("marker %q %v", data, err)
	}
	if _, err := os.Stat(marker + ".new"); !os.IsNotExist(err) {
		t.Fatal("temporary marker left behind")
	}
	if info, _ := os.Stat(marker); runtime.GOOS != "windows" && info.Mode().Perm() != 0o600 {
		t.Fatalf("marker mode %v", info.Mode())
	}

	missingKeys := t.TempDir()
	wdttPlusFiles(t, missingKeys, "etc/wdtt/passwords.json")
	if out := wdttPlusRoot(t, missingKeys, wdttPlusMarkPreserved); strings.Contains(out, "ready") {
		t.Fatal("marked data without WireGuard keys")
	}
	if _, err := os.Stat(filepath.Join(missingKeys, "etc/wdtt/.android-deploy-preserved")); !os.IsNotExist(err) {
		t.Fatal("marker written for incomplete data")
	}
	if runtime.GOOS != "windows" {
		linked := t.TempDir()
		wdttPlusFiles(t, linked, "etc/wdtt/wg-keys.dat", "elsewhere.json")
		if err := os.Symlink(filepath.Join(linked, "elsewhere.json"), filepath.Join(linked, "etc/wdtt/passwords.json")); err != nil {
			t.Fatal(err)
		}
		if out := wdttPlusRoot(t, linked, wdttPlusMarkPreserved); strings.Contains(out, "ready") {
			t.Fatal("marked a symlinked database")
		}
	}
}

func TestWdttPlusPreparationFollowsTheProbe(t *testing.T) {
	if got, err := wdttPlusPreparation("WDTT_DB=1\nWDTT_SERVICE=none\n"); err != nil || got != wdttPlusMarkPreserved {
		t.Fatalf("kept data must be marked: %q %v", got, err)
	}
	for _, probe := range []string{"WDTT_DB=1\nWDTT_SERVICE=partial\n", "WDTT_DB=1\n", ""} {
		if _, err := wdttPlusPreparation(probe); err == nil {
			t.Fatalf("updated past %q", probe)
		}
	}
	// A failed start leaves the marker next to the installed service; the next update must drop it.
	root := t.TempDir()
	wdttPlusFiles(t, root, "etc/wdtt/passwords.json", "etc/wdtt/wg-keys.dat", "etc/wdtt/.android-deploy-preserved",
		"etc/systemd/system/wdtt.service", "usr/local/bin/wdtt-server")
	clear, err := wdttPlusPreparation(wdttPlusRoot(t, root, wdttPlusProbe))
	if err != nil {
		t.Fatal(err)
	}
	if out := wdttPlusRoot(t, root, clear); !strings.Contains(out, "WDTT_PRESERVED_MARKER=cleared") {
		t.Fatalf("stale marker kept: %s", out)
	}
	if _, err := os.Stat(filepath.Join(root, "etc/wdtt/.android-deploy-preserved")); !os.IsNotExist(err) {
		t.Fatal("stale marker still on the server")
	}
	if _, err := os.Stat(filepath.Join(root, "etc/wdtt/passwords.json")); err != nil {
		t.Fatal("the access database was touched")
	}
}
