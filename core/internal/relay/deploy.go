package relay

import (
	"archive/zip"
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"github.com/pkg/sftp"
	"golang.org/x/crypto/ssh"
	"io"
	"math"
	"net"
	"net/http"
	"net/url"
	"os"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
)

var addressPattern = regexp.MustCompile(`^[A-Za-z0-9.:_-]+$`)
var userPattern = regexp.MustCompile(`^[A-Za-z0-9_.-]+$`)
var panelPattern = regexp.MustCompile(`^[A-Za-z0-9]+$`)
var adminPinPattern = regexp.MustCompile(`WDTT_ADMIN_PIN\|(sha256/[A-Za-z0-9+/=]+)`)

func uploadFile(files *sftp.Client, path string, data []byte) error {
	f, e := files.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_TRUNC)
	if e != nil {
		return e
	}
	if e = f.Chmod(0600); e != nil {
		f.Close()
		return e
	}
	_, e = f.Write(data)
	closeErr := f.Close()
	if e != nil {
		return e
	}
	return closeErr
}

func port(s map[string]string, key string) (string, error) {
	n, e := strconv.Atoi(s[key])
	if e != nil || n < 1 || n > 65535 {
		return "", fmt.Errorf("Некорректный порт: %s", key)
	}
	return strconv.Itoa(n), nil
}
func validateServer(s map[string]string, install bool) error {
	if s["kind"] != "qwdtt" && s["kind"] != "csqtt" {
		return fmt.Errorf("Неизвестный протокол")
	}
	if !addressPattern.MatchString(s["host"]) || !userPattern.MatchString(s["user"]) {
		return fmt.Errorf("Укажите IP/домен и логин SSH")
	}
	keys := []string{"sshPort", "peerPort"}
	if s["kind"] == "qwdtt" {
		keys = append(keys, "wgPort", "rawPort", "adminPort", "directPort")
	} else {
		keys = append(keys, "webPort")
	}
	for _, k := range keys {
		if _, e := port(s, k); e != nil {
			return e
		}
	}
	if s["auth"] == "key" {
		if s["key"] == "" {
			return fmt.Errorf("Загрузите SSH-ключ")
		}
	} else if s["sshPassword"] == "" {
		return fmt.Errorf("Введите пароль SSH")
	}
	if !install {
		return nil
	}
	if s["password"] == "" || strings.Contains(s["password"], "|") {
		return fmt.Errorf("Задайте пароль туннеля")
	}
	for _, k := range []string{"password", "webUser", "webPassword", "botToken", "adminToken", "adminId", "deviceId"} {
		if strings.ContainsAny(s[k], "\r\n\x00") {
			return fmt.Errorf("Некорректное значение: %s", k)
		}
	}
	if s["kind"] == "csqtt" {
		if !panelPattern.MatchString(s["webUser"]) || !panelPattern.MatchString(s["webPassword"]) {
			return fmt.Errorf("Логин и пароль панели: латиница и цифры")
		}
	} else {
		for _, k := range []string{"dns1", "dns2"} {
			if ip := net.ParseIP(s[k]); ip == nil || ip.To4() == nil {
				return fmt.Errorf("DNS должен быть IPv4-адресом")
			}
		}
		if s["adminToken"] == "" {
			return fmt.Errorf("Не задан токен панели")
		}
	}
	return nil
}
func artifact(ctx context.Context, kind string) (*zip.Reader, error) {
	source, digest := "https://github.com/makriq-org/qwdtt/releases/download/v1.4.4/qWDTT_v1.4.4.apk", "ff5b2d1f265b0c4253cdf54ebf1f8a5d64afd1424086aa8b8be754b5c14df6a7"
	if kind == "csqtt" {
		source = "https://github.com/amurcanov/csqtt/releases/download/v2.1.9/CSQTT-universal.apk"
		digest = "49771d60a53d731452f3301e53dbe6bdee9142876e6b295dbf88f59f0b5aaf82"
	}
	client := &http.Client{Timeout: 5 * time.Minute}
	request, e := http.NewRequestWithContext(ctx, http.MethodGet, source, nil)
	if e != nil {
		return nil, e
	}
	response, e := client.Do(request)
	if e != nil {
		return nil, e
	}
	defer response.Body.Close()
	if response.StatusCode != 200 {
		return nil, fmt.Errorf("Загрузка установщика: HTTP %d", response.StatusCode)
	}
	data, e := io.ReadAll(io.LimitReader(response.Body, 250*1024*1024+1))
	if e != nil {
		return nil, e
	}
	sum := sha256.Sum256(data)
	if len(data) > 250*1024*1024 || hex.EncodeToString(sum[:]) != digest {
		return nil, fmt.Errorf("SHA-256 установщика не совпадает")
	}
	return zip.NewReader(bytes.NewReader(data), int64(len(data)))
}
func zipEntry(z *zip.Reader, name string) ([]byte, error) {
	for _, f := range z.File {
		if f.Name == name {
			if f.UncompressedSize64 == 0 || f.UncompressedSize64 > 30_000_000 {
				return nil, fmt.Errorf("Некорректный размер файла установщика")
			}
			r, e := f.Open()
			if e != nil {
				return nil, e
			}
			defer r.Close()
			return io.ReadAll(io.LimitReader(r, 30_000_001))
		}
	}
	return nil, fmt.Errorf("Нет файла установщика: %s", name)
}
func (p *protocol) deploy(r Request) error {
	s := r.Server
	install := r.Operation == "deploy"
	if e := validateServer(s, install); e != nil {
		return e
	}
	for _, k := range []string{"sshPassword", "key", "keyPassword", "sudoPassword", "password", "webPassword", "botToken", "adminToken"} {
		p.secrets = append(p.secrets, s[k])
	}
	var auth ssh.AuthMethod
	if s["auth"] == "key" {
		var signer ssh.Signer
		var e error
		if s["keyPassword"] != "" {
			signer, e = ssh.ParsePrivateKeyWithPassphrase([]byte(s["key"]), []byte(s["keyPassword"]))
		} else {
			signer, e = ssh.ParsePrivateKey([]byte(s["key"]))
		}
		if e != nil {
			return fmt.Errorf("Не удалось прочитать SSH-ключ")
		}
		auth = ssh.PublicKeys(signer)
	} else {
		auth = ssh.Password(s["sshPassword"])
	}
	cfg := &ssh.ClientConfig{User: s["user"], Auth: []ssh.AuthMethod{auth}, Timeout: 30 * time.Second, HostKeyCallback: func(host string, remote net.Addr, key ssh.PublicKey) error {
		fingerprint := ssh.FingerprintSHA256(key)
		if s["hostKey"] != "" {
			if fingerprint != s["hostKey"] {
				return fmt.Errorf("SSH-ключ сервера изменился")
			}
			return nil
		}
		p.emit("trust", fingerprint, nil)
		var answer string
		select {
		case answer = <-p.commands:
		case <-p.ctx.Done():
			return p.ctx.Err()
		case <-time.After(2 * time.Minute):
			return fmt.Errorf("SSH-ключ не подтверждён")
		}
		if answer != "approve" {
			return fmt.Errorf("SSH-ключ сервера не подтверждён")
		}
		p.emit("host-key", fingerprint, nil)
		return nil
	}}
	p.emit("progress", "Подключение к VPS", 5)
	address := net.JoinHostPort(strings.Trim(s["host"], "[]"), s["sshPort"])
	connection, e := (&net.Dialer{Timeout: 30 * time.Second}).DialContext(p.ctx, "tcp", address)
	if e != nil {
		return e
	}
	_ = connection.SetDeadline(time.Now().Add(150 * time.Second))
	sshConnection, channels, requests, e := ssh.NewClientConn(connection, address, cfg)
	if e != nil {
		connection.Close()
		return e
	}
	_ = connection.SetDeadline(time.Time{})
	client := ssh.NewClient(sshConnection, channels, requests)
	defer client.Close()
	go func() { <-p.ctx.Done(); _ = client.Close() }()
	exec := func(command string) (string, error) {
		session, e := client.NewSession()
		if e != nil {
			return "", e
		}
		defer session.Close()
		stdin, e := session.StdinPipe()
		if e != nil {
			return "", e
		}
		var output bytes.Buffer
		writer := &deployWriter{p: p, buffer: &output, kind: s["kind"]}
		session.Stdout = writer
		session.Stderr = writer
		if e = session.Start(command); e != nil {
			return "", e
		}
		go func() {
			password := s["sudoPassword"]
			if password == "" {
				password = s["sshPassword"]
			}
			_, _ = io.WriteString(stdin, password+"\n")
			_ = stdin.Close()
		}()
		done := make(chan error, 1)
		go func() { done <- session.Wait() }()
		select {
		case e = <-done:
		case <-time.After(30 * time.Minute):
			_ = session.Close()
			e = fmt.Errorf("Превышено время установки")
		case <-p.ctx.Done():
			_ = session.Close()
			e = p.ctx.Err()
		}
		writer.flush()
		writer.mu.Lock()
		result := output.String()
		writer.mu.Unlock()
		return result, e
	}
	machine, e := exec("uname -m")
	if e != nil {
		return e
	}
	machine = strings.TrimSpace(machine)
	binary := "assets/server"
	if s["kind"] == "qwdtt" && machine != "x86_64" && install {
		return fmt.Errorf("Официальный сервер qWDTT поддерживает x86_64")
	}
	if s["kind"] == "csqtt" {
		switch machine {
		case "x86_64", "amd64":
			binary = "assets/csqtt-linux-amd64"
		case "aarch64", "arm64":
			binary = "assets/csqtt-linux-arm64"
		case "armv7l", "armv7":
			binary = "assets/csqtt-linux-armv7"
		default:
			return fmt.Errorf("Архитектура VPS не поддерживается: %s", machine)
		}
	}
	p.emit("progress", "Загрузка официального установщика", 10)
	apk, e := artifact(p.ctx, s["kind"])
	if e != nil {
		return e
	}
	files, e := sftp.NewClient(client)
	if e != nil {
		return e
	}
	defer files.Close()
	upload := func(path string, data []byte) error {
		return uploadFile(files, path, data)
	}
	script, e := zipEntry(apk, "assets/deploy.sh")
	if e != nil {
		return e
	}
	if e = upload("/tmp/deploy.sh", script); e != nil {
		return e
	}
	if install {
		p.emit("progress", "Передача файлов на VPS", 30)
		data, e := zipEntry(apk, binary)
		if e != nil {
			return e
		}
		target := "/tmp/.csqtt-upload-server"
		if s["kind"] == "qwdtt" {
			target = "/tmp/wdtt-server"
			if bytes.Contains(data, []byte("/etc/wireguard")) || (bytes.Contains(data, []byte("wg0")) && !bytes.Contains(data, []byte("wdtt0"))) {
				return fmt.Errorf("Устаревший сервер qWDTT: требуется wdtt0 и /etc/wdtt")
			}
		}
		if e = upload(target, data); e != nil {
			return e
		}
		uploads := map[string][]byte{}
		if s["kind"] == "qwdtt" {
			uploads["/tmp/wdtt-main.password"] = []byte(s["password"])
			uploads["/tmp/wdtt-admin.token"] = []byte(s["adminToken"])
			uploads["/tmp/wdtt-bot.token"] = []byte(s["botToken"])
		} else {
			uploads["/tmp/.csqtt-upload-web.env"] = []byte("CSQTT_WEB_USER=" + s["webUser"] + "\nCSQTT_WEB_PASS=" + s["webPassword"] + "\n")
			uploads["/tmp/.csqtt-upload-overrides.json"], _ = json.Marshal(map[string]string{"main_password": s["password"], "device_id": s["deviceId"]})
		}
		for path, data := range uploads {
			if e = upload(path, data); e != nil {
				return e
			}
		}
	}
	env := "CSQTT_PEER_PORT=" + s["peerPort"] + " CSQTT_WEB_PORT=" + s["webPort"] + " CSQTT_SSH_PORT=" + s["sshPort"] + " CSQTT_DEPLOY_MODE=systemd"
	if s["docker"] == "true" {
		env = strings.Replace(env, "=systemd", "=docker", 1)
	}
	if s["kind"] == "qwdtt" {
		env = "WDTT_DTLS_PORT=" + s["peerPort"] + " WDTT_WG_PORT=" + s["wgPort"] + " WDTT_RAW_PORT=" + s["rawPort"] + " WDTT_DIRECT_PORT=" + s["directPort"] + " WDTT_ADMIN_PORT=" + s["adminPort"] + " WDTT_SSH_PORT=" + s["sshPort"] + " WDTT_ADMIN_ID=" + quote(s["adminId"]) + " WDTT_DNS_SERVERS=" + quote(s["dns1"]+","+s["dns2"])
	}
	action := "install"
	if !install {
		action = "uninstall"
	}
	command := "env " + env + " bash /tmp/deploy.sh " + action
	root := "if [ \"$(id -u)\" = 0 ]; then bash -c " + quote(command) + "; else sudo -S -p '' bash -c " + quote(command) + "; fi"
	p.emit("progress", "Выполнение установщика", 45)
	output, e := exec(root)
	if e != nil {
		return fmt.Errorf("Установщик завершился с ошибкой: %w", e)
	}
	if install {
		marker := "CSQTT_DEPLOY_OK"
		if s["kind"] == "qwdtt" {
			marker = "WDTT_DEPLOY_OK"
			pin := adminPinPattern.FindStringSubmatch(output)
			if len(pin) != 2 {
				return fmt.Errorf("Не получен TLS-отпечаток панели")
			}
			p.emit("admin-pin", pin[1], nil)
		}
		if !strings.Contains(output, marker) {
			return fmt.Errorf("Установщик не подтвердил запуск сервиса")
		}
		query := url.Values{"peer": {net.JoinHostPort(s["host"], s["peerPort"])}, "pass": {s["password"]}, "device_id": {s["deviceId"]}, "name": {s["name"]}}
		if s["kind"] == "qwdtt" {
			query.Set("mode", "raw")
			query.Set("raw_port", s["rawPort"])
			p.emit("config", "", "qwdtt://config?"+query.Encode())
		} else {
			query = url.Values{"v": {"2"}, "host": {s["host"]}, "peer": {s["peerPort"]}, "password": {s["password"]}, "device_id": {s["deviceId"]}}
			p.emit("config", "", "csqtt://connect?"+query.Encode())
		}
	} else {
		p.emit("config", "", "")
	}
	p.emit("progress", "Готово", 100)
	return nil
}

type deployWriter struct {
	p       *protocol
	buffer  *bytes.Buffer
	mu      sync.Mutex
	pending string
	kind    string
}

func (w *deployWriter) Write(b []byte) (int, error) {
	w.mu.Lock()
	defer w.mu.Unlock()
	if w.buffer.Len() < 2*1024*1024 {
		w.buffer.Write(b)
	}
	w.pending += string(b)
	for {
		at := strings.IndexByte(w.pending, '\n')
		if at < 0 {
			break
		}
		line := w.pending[:at+1]
		w.p.emit("log", line, nil)
		parts := strings.SplitN(strings.TrimSpace(line), "|", 3)
		prefix := "WDTT_PROGRESS"
		if w.kind == "csqtt" {
			prefix = "CSQTT_PROGRESS"
		}
		if len(parts) == 3 && parts[0] == prefix {
			if value, e := strconv.ParseFloat(parts[1], 64); e == nil && !math.IsNaN(value) && !math.IsInf(value, 0) && value >= 0 && value <= 1 {
				w.p.emit("progress", parts[2], int(value*99))
			}
		}
		w.pending = w.pending[at+1:]
	}
	if len(w.pending) > 1024*1024 {
		w.pending = ""
		w.p.emit("log", "[Слишком длинная строка установщика]\n", nil)
	}
	return len(b), nil
}
func (w *deployWriter) flush() {
	w.mu.Lock()
	defer w.mu.Unlock()
	if w.pending != "" {
		w.p.emit("log", w.pending, nil)
		w.pending = ""
	}
}
