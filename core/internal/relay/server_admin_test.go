package relay

import (
	"bytes"
	"compress/gzip"
	"crypto/aes"
	"crypto/cipher"
	"crypto/pbkdf2"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func adminServer(t *testing.T) (*httptest.Server, map[string]string) {
	t.Helper()
	server := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("Authorization") != "Bearer admin-token" {
			w.WriteHeader(http.StatusUnauthorized)
			io.WriteString(w, `{"error":"unauthorized"}`)
			return
		}
		switch r.URL.Path {
		case "/admin/passwords":
			if r.Method == http.MethodGet {
				io.WriteString(w, `{"passwords":[{"password":"p1","label":"Анна"}]}`)
				return
			}
			r.ParseForm()
			io.WriteString(w, `{"password":"new","vk_hash":"`+r.PostForm.Get("vk_hash")+`","label":"`+r.PostForm.Get("label")+`"}`)
		case "/admin/passwords/delete":
			w.WriteHeader(http.StatusNotFound)
			io.WriteString(w, `{"error":"нет такого пароля"}`)
		}
	}))
	t.Cleanup(server.Close)
	sum := sha256.Sum256(server.Certificate().Raw)
	host, port, _ := net.SplitHostPort(strings.TrimPrefix(server.URL, "https://"))
	return server, map[string]string{"kind": "qwdtt", "host": host, "adminPort": port, "adminToken": "admin-token",
		"adminPin": "sha256/" + base64.StdEncoding.EncodeToString(sum[:])}
}

func adminProtocol(t *testing.T) *protocol {
	p := &protocol{output: json.NewEncoder(io.Discard)}
	p.ctx = t.Context()
	return p
}

func TestQwdttAdminPinsTheCertificate(t *testing.T) {
	_, s := adminServer(t)
	p := adminProtocol(t)
	value, err := p.qwdttAdmin(s, `{"path":"/admin/passwords"}`)
	if err != nil {
		t.Fatal(err)
	}
	if list := value.(map[string]any)["passwords"].([]any); len(list) != 1 {
		t.Fatalf("list %v", value)
	}
	value, err = p.qwdttAdmin(s, `{"path":"/admin/passwords","form":{"vk_hash":"a,b","label":"Иван"}}`)
	if err != nil || value.(map[string]any)["vk_hash"] != "a,b" || value.(map[string]any)["label"] != "Иван" {
		t.Fatalf("create %v %v", value, err)
	}
	if _, err = p.qwdttAdmin(s, `{"path":"/admin/passwords/delete","form":{"password":"x"}}`); err == nil || err.Error() != "нет такого пароля" {
		t.Fatalf("server error message %v", err)
	}
	if _, err = p.qwdttAdmin(s, `{"path":"/admin/other"}`); err == nil {
		t.Fatal("a path outside the admin API was called")
	}
	wrong := map[string]string{}
	for k, v := range s {
		wrong[k] = v
	}
	other := sha256.Sum256([]byte("another certificate"))
	wrong["adminPin"] = "sha256/" + base64.StdEncoding.EncodeToString(other[:])
	if _, err = p.qwdttAdmin(wrong, `{"path":"/admin/passwords"}`); err == nil {
		t.Fatal("a different certificate was trusted")
	}
	wrong["adminPin"] = ""
	if _, err = p.qwdttAdmin(wrong, `{"path":"/admin/passwords"}`); err == nil {
		t.Fatal("no pin, no request")
	}
}

func TestWdttPlusAdminInputAndAnswers(t *testing.T) {
	s := validWdttPlusServer()
	input := wdttPlusAdminInput(s, []string{"list"})
	if input != `{"args":["list"],"main_password":"main-password"}`+"\n" {
		t.Fatalf("root input %q", input)
	}
	s["user"], s["sudoPassword"] = "admin", "sudo-pass"
	if input = wdttPlusAdminInput(s, []string{"list"}); !strings.HasPrefix(input, "sudo-pass\n{") {
		t.Fatalf("sudo goes first: %q", input)
	}
	if v, err := wdttPlusAdminAnswer("noise\n{\"ok\":true,\"passwords\":[]}\n"); err != nil || v["ok"] != true {
		t.Fatalf("ok answer %v %v", v, err)
	}
	for output, want := range map[string]string{
		`{"ok":false,"message":"no_passwords_json"}`:     "Сначала выполните деплой",
		`{"ok":false,"message":"main_password invalid"}`: "Главный пароль не подходит",
		"bash: wdtt-server: command not found":           "command not found",
	} {
		if _, err := wdttPlusAdminAnswer(output); err == nil || !strings.Contains(err.Error(), want) {
			t.Fatalf("%q -> %v", output, err)
		}
	}
}

func TestWdttPlusAdminOverSSH(t *testing.T) {
	var commands []string
	f := startFakeSSH(t, func(command string) (string, uint32) {
		commands = append(commands, command)
		return `{"ok":true,"server":{"wdtt_plus_version":"20"},"passwords":[]}` + "\n", 0
	})
	s := validWdttPlusServer()
	s["host"], s["sshPort"] = f.host, f.port
	p := adminProtocol(t)
	if _, err := p.wdttPlusAdmin(s, `{"args":["list"]}`); err == nil {
		t.Fatal("admin without the host key remembered at deploy")
	}
	s["hostKey"] = f.fingerprint
	value, err := p.wdttPlusAdmin(s, `{"args":["list"]}`)
	if err != nil || value.(map[string]any)["server"].(map[string]any)["wdtt_plus_version"] != "20" {
		t.Fatalf("list %v %v", value, err)
	}
	if len(commands) != 1 || !strings.Contains(commands[0], wdttPlusAdminCommand) || strings.Contains(commands[0], "main-password") {
		t.Fatalf("admin command %v", commands)
	}
}

func TestWdttPlusBackupTransfer(t *testing.T) {
	file := func(path, data string) map[string]any {
		return map[string]any{"path": path, "data_b64": base64.StdEncoding.EncodeToString([]byte(data))}
	}
	snapshot := map[string]any{"format": "wdtt-server-snapshot", "created_at": float64(1700000000), "device_count": float64(3),
		"files": []any{file("passwords.json", `{"passwords":{"a":{},"b":{}}}`), file("wg-keys.dat", "keys")}}
	document, err := wdttPlusPortableBackup(snapshot, "203.0.113.5")
	if err != nil {
		t.Fatal(err)
	}
	var portable map[string]any
	json.Unmarshal([]byte(document), &portable)
	if portable["format"] != "wdtt-server-backup" || portable["version"] != float64(4) || portable["password_count"] != float64(2) ||
		portable["created_at"] != "2023-11-14T22:13:20Z" || portable["source_host"] != "203.0.113.5" {
		t.Fatalf("portable %v", portable)
	}
	if _, err = wdttPlusPortableBackup(map[string]any{"format": "other"}, ""); err == nil {
		t.Fatal("foreign snapshot accepted")
	}
	if _, err = wdttPlusEncryptTransfer(document, "short"); err == nil {
		t.Fatal("a short file password accepted")
	}
	sealed, err := wdttPlusEncryptTransfer(document, "file-password")
	if err != nil {
		t.Fatal(err)
	}
	var container struct {
		Format, Kind, KDF, Salt, IV, Data string
		Iterations                        int
	}
	json.Unmarshal([]byte(sealed), &container)
	if container.Format != "wdtt-plus-transfer" || container.Kind != "server-backup" || container.KDF != "PBKDF2-HMAC-SHA256" || container.Iterations != 210000 {
		t.Fatalf("container %+v", container)
	}
	salt, _ := base64.StdEncoding.DecodeString(container.Salt)
	iv, _ := base64.StdEncoding.DecodeString(container.IV)
	data, _ := base64.StdEncoding.DecodeString(container.Data)
	key, _ := pbkdf2.Key(sha256.New, "file-password", salt, 210000, 32)
	block, _ := aes.NewCipher(key)
	gcm, _ := cipher.NewGCM(block)
	plain, err := gcm.Open(nil, iv, data, nil)
	if err != nil {
		t.Fatal(err)
	}
	reader, _ := gzip.NewReader(bytes.NewReader(plain))
	restored, _ := io.ReadAll(reader)
	if string(restored) != document {
		t.Fatal("the container does not hold the document")
	}
}
