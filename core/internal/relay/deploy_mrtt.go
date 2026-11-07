package relay

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/pkg/sftp"
	"golang.org/x/crypto/ssh"
)

// MRTT's own server files of the release this client carries: deploy.sh, cascade.sh, the server and the cascade client for each architecture.
const (
	mrttServerBundle       = "https://github.com/ttrroyy/qThroneMobile/releases/download/v1.0.0/mrtt-server-233eddc0.tar.gz"
	mrttServerBundleSHA256 = "bb94501c7c762bb24297f3a36d94b0d1c74e7fed8125f9a89b6a21027da2c503"
)

var mrttTunnelPassword = regexp.MustCompile(`^[^|\s\p{Cc}]+$`)

func validateMRTT(s map[string]string, install bool) error {
	if !addressPattern.MatchString(s["host"]) || strings.Contains(s["host"], ":") || !userPattern.MatchString(s["user"]) {
		return fmt.Errorf("Укажите IP/домен и логин SSH")
	}
	for _, k := range []string{"sshPort", "peerPort", "webPort"} {
		if _, e := port(s, k); e != nil {
			return e
		}
	}
	if s["peerPort"] == s["webPort"] {
		return fmt.Errorf("Порт подключения и порт API должны различаться")
	}
	if s["auth"] == "key" && s["key"] == "" {
		return fmt.Errorf("Загрузите SSH-ключ")
	}
	if s["auth"] != "key" && s["sshPassword"] == "" {
		return fmt.Errorf("Введите пароль SSH")
	}
	if install && (len(s["password"]) > 128 || !mrttTunnelPassword.MatchString(s["password"])) {
		return fmt.Errorf("Пароль владельца: без пробелов и «|», не длиннее 128 байт")
	}
	return nil
}

// Output markers of the original deploy.sh; the exit codes mean what its app reads them as.
func mrttDeployFailure(status int, output string) string {
	detail := ""
	for _, line := range strings.Split(output, "\n") {
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, "MRTT_DEPLOY_ERROR|") {
			if parts := strings.SplitN(line, "|", 3); len(parts) == 3 && strings.TrimSpace(parts[2]) != "" {
				detail = strings.TrimSpace(parts[2])
			}
		}
	}
	if len(detail) > 360 {
		detail = detail[:360]
	}
	with := func(message string) string {
		if detail == "" {
			return message
		}
		return message + " Причина: " + detail
	}
	switch {
	case strings.Contains(output, "MRTT_DEPLOY_ROLLED_BACK") || status == 30:
		return with("Новый релиз не прошёл проверку; предыдущая установка восстановлена")
	case strings.Contains(output, "MRTT_DEPLOY_ROLLBACK_FAILED") || status == 31:
		return with("Новый релиз не запустился, а автоматический rollback завершился с ошибкой — требуется проверка VPS")
	case status == 20:
		return with("Кандидат не прошёл предзапусковую проверку; работающий сервер не останавливался")
	case status == 2:
		return with("Установщик отклонил параметры развёртывания")
	case detail != "":
		return detail
	}
	return fmt.Sprintf("Установщик завершился с кодом %d", status)
}

func mrttDeployed(output string) bool {
	for _, line := range strings.Split(output, "\n") {
		if strings.TrimSpace(line) == "MRTT_DEPLOY_OK" {
			return true
		}
	}
	return false
}

func mrttServerAsset(machine string) (string, error) {
	switch strings.ToLower(strings.TrimSpace(machine)) {
	case "x86_64", "amd64":
		return "mrtt-linux-amd64", nil
	case "aarch64", "arm64":
		return "mrtt-linux-arm64", nil
	case "armv7l", "armv7", "armhf":
		return "mrtt-linux-armv7", nil
	}
	return "", fmt.Errorf("Архитектура VPS %s пока не поддерживается. Поддерживаются: x86_64, aarch64, armv7l", strings.TrimSpace(machine))
}

func (p *protocol) mrttBundle() (map[string][]byte, error) {
	request, e := http.NewRequestWithContext(p.ctx, http.MethodGet, mrttServerBundle, nil)
	if e != nil {
		return nil, e
	}
	response, e := (&http.Client{Timeout: 5 * time.Minute}).Do(request)
	if e != nil {
		return nil, e
	}
	defer response.Body.Close()
	if response.StatusCode != 200 {
		return nil, fmt.Errorf("Загрузка файлов сервера: HTTP %d", response.StatusCode)
	}
	data, e := io.ReadAll(io.LimitReader(response.Body, 200*1024*1024+1))
	if e != nil {
		return nil, e
	}
	sum := sha256.Sum256(data)
	if hex.EncodeToString(sum[:]) != mrttServerBundleSHA256 {
		return nil, fmt.Errorf("SHA-256 файлов сервера не совпадает")
	}
	unpacked, e := gzip.NewReader(bytes.NewReader(data))
	if e != nil {
		return nil, e
	}
	files := map[string][]byte{}
	archive := tar.NewReader(unpacked)
	for {
		header, e := archive.Next()
		if e == io.EOF {
			return files, nil
		}
		if e != nil {
			return nil, e
		}
		if files[header.Name], e = io.ReadAll(archive); e != nil {
			return nil, e
		}
	}
}

func (p *protocol) deployMRTT(client *ssh.Client, s map[string]string, install bool, machine string) error {
	asset, e := mrttServerAsset(machine)
	if install && e != nil {
		return e
	}
	p.emit("progress", "Загрузка файлов сервера", 5)
	bundle, e := p.mrttBundle()
	if e != nil {
		return e
	}
	files, e := sftp.NewClient(client)
	if e != nil {
		return e
	}
	defer files.Close()
	if e = uploadFile(files, "/tmp/deploy.sh", bundle["deploy.sh"]); e != nil {
		return e
	}
	host := strings.TrimSpace(s["host"])
	mode := "systemd"
	publicIP := `"` + strings.NewReplacer(`\`, `\\`, `"`, `\"`).Replace(host) + `"`
	if s["docker"] == "true" {
		mode, publicIP = "docker", host
	}
	if !install {
		p.emit("progress", "Удаление через deploy.sh", 30)
		_, _ = p.run(client, s, asRoot("env MRTT_PEER_PORT="+s["peerPort"]+" MRTT_SSH_PORT="+s["sshPort"]+" bash /tmp/deploy.sh uninstall"), 5*time.Minute, true)
		// The cascade clients belong to MRTT and leave with it.
		// shortcut: their iptables rules stay until a reboot, without the interface they act on nothing.
		_, _ = p.run(client, s, asRoot(`for u in /etc/systemd/system/mrtt-exit-*.service; do [ -e "$u" ] || continue; systemctl disable --now "$(basename "$u")" >/dev/null 2>&1; rm -f "$u"; done; rm -rf /etc/mrtt-exit /usr/local/lib/mrtt-exit-client; systemctl daemon-reload`), time.Minute, true)
		p.emit("config", "", "")
		p.emit("progress", "Готово", 100)
		return nil
	}
	p.emit("progress", "Загрузка нового сервера", 10)
	if e = uploadFile(files, "/tmp/.mrtt-upload-server", bundle[asset]); e != nil {
		return e
	}
	if e = uploadFile(files, "/tmp/.mrtt-upload-web.env", []byte("MRTT_PUBLIC_IP="+publicIP+"\n")); e != nil {
		return e
	}
	overrides, _ := json.Marshal(map[string]string{"main_password": s["password"]})
	if e = uploadFile(files, "/tmp/.mrtt-upload-overrides.json", overrides); e != nil {
		return e
	}
	p.emit("progress", "Установка нового сервера", 18)
	environment := "env MRTT_PEER_PORT=" + s["peerPort"] + " MRTT_SSH_PORT=" + s["sshPort"] + " MRTT_WEB_PORT=" + s["webPort"] + " MRTT_PUBLIC_IP=" + quote(host) + " MRTT_DEPLOY_MODE=" + mode
	output, e := p.run(client, s, asRoot(environment+" bash /tmp/deploy.sh install"), 30*time.Minute, true)
	status := 0
	if e != nil {
		status = -1
		if failed, ok := e.(*ssh.ExitError); ok {
			status = failed.ExitStatus()
		}
	}
	if status != 0 || !mrttDeployed(output) {
		return fmt.Errorf("%s", mrttDeployFailure(status, output))
	}
	// The owner's link is what this deployment gives; the server may still be starting its API.
	link := "MRTT установлен"
	for i := 0; i < 10; i++ {
		if listed, e := mrttRequest(client, s, "GET", "/api/links", ""); e == nil {
			var links []struct {
				Link string `json:"link"`
			}
			if json.Unmarshal([]byte(listed), &links) == nil && len(links) > 0 && links[0].Link != "" {
				link = links[0].Link
				break
			}
		}
		time.Sleep(time.Second)
	}
	p.emit("config", "", link)
	p.emit("progress", "Готово", 100)
	return nil
}

// mrttRequest is one call of the server's management API: it listens on 127.0.0.1 only, so it goes through SSH with the deploy's access.
func mrttRequest(client *ssh.Client, s map[string]string, method, path, body string) (string, error) {
	connection, e := client.Dial("tcp", net.JoinHostPort("127.0.0.1", s["webPort"]))
	if e != nil {
		return "", fmt.Errorf("MRTT не установлен")
	}
	defer connection.Close()
	_ = connection.SetDeadline(time.Now().Add(20 * time.Second))
	// The server puts the Host header into client links: the VPS address, not 127.0.0.1.
	head := method + " " + path + " HTTP/1.0\r\nHost: " + s["host"] + "\r\nAccept: application/json\r\nConnection: close\r\n"
	if body != "" {
		head += "Content-Type: application/json\r\n"
	}
	head += "Content-Length: " + strconv.Itoa(len(body)) + "\r\n\r\n"
	if _, e = io.WriteString(connection, head+body); e != nil {
		return "", e
	}
	raw, e := io.ReadAll(io.LimitReader(connection, 32*1024*1024))
	if e != nil && len(raw) == 0 {
		return "", e
	}
	return parseMRTTResponse(raw)
}

func parseMRTTResponse(raw []byte) (string, error) {
	text := string(raw)
	split := strings.Index(text, "\r\n\r\n")
	if split < 0 {
		return "", fmt.Errorf("Сервер не ответил: API управления недоступно")
	}
	fields := strings.Fields(text[:split])
	if len(fields) < 2 {
		return "", fmt.Errorf("Некорректный ответ API")
	}
	status, e := strconv.Atoi(fields[1])
	if e != nil {
		return "", fmt.Errorf("Некорректный ответ API")
	}
	body := text[split+4:]
	if status < 200 || status > 299 {
		message := strings.TrimSpace(body)
		if message == "" {
			message = "HTTP " + fields[1]
		}
		if len(message) > 300 {
			message = message[:300]
		}
		return "", fmt.Errorf("%s", message)
	}
	return body, nil
}

func mrttCascadeRun(p *protocol, client *ssh.Client, s map[string]string, script, marker string, arguments ...string) ([]string, error) {
	quoted := make([]string, len(arguments))
	for i, value := range arguments {
		quoted[i] = quote(value)
	}
	output, e := p.run(client, s, asRoot("set -- "+strings.Join(quoted, " ")+"\n"+script), 3*time.Minute, false)
	title := s["name"]
	if title == "" {
		title = s["host"]
	}
	line := ""
	reason := ""
	for _, item := range strings.Split(output, "\n") {
		if line == "" && strings.HasPrefix(item, marker) {
			line = item
		}
		if strings.HasPrefix(item, "error:") {
			reason = strings.TrimSpace(strings.TrimPrefix(item, "error:"))
		}
	}
	if e != nil || line == "" {
		if reason == "" {
			reason = "каскад не настроился"
		}
		return nil, fmt.Errorf("%s: %s", title, reason)
	}
	return strings.Fields(line), nil
}

// The first server connects to the second as an ordinary config; on an error it removes what it set up.
func (p *protocol) mrttAddExit(from *ssh.Client, s, target map[string]string) (map[string]any, error) {
	if target["hostKey"] == "" {
		return nil, fmt.Errorf("Сначала установите MRTT на втором сервере из этого приложения")
	}
	remote, e := p.dial(target, false)
	if e != nil {
		return nil, e
	}
	defer remote.Close()
	bytesID := make([]byte, 4)
	if _, e = rand.Read(bytesID); e != nil {
		return nil, e
	}
	id := hex.EncodeToString(bytesID)
	name := "Каскад " + id
	body, _ := json.Marshal(map[string]any{"name": name, "group_id": "", "days": 0, "hash": "", "vk_token": false, "workers": 0, "max_devices": 0, "transport": "", "link_room_off": false, "exit": ""})
	if _, e = mrttRequest(remote, target, "POST", "/api/clients", string(body)); e != nil {
		return nil, e
	}
	listed, e := mrttRequest(remote, target, "GET", "/api/clients", "")
	if e != nil {
		return nil, e
	}
	var clients []struct {
		Name     string `json:"name"`
		Password string `json:"password"`
	}
	password := ""
	if json.Unmarshal([]byte(listed), &clients) == nil {
		for _, client := range clients {
			if client.Name == name {
				password = client.Password
			}
		}
	}
	if password == "" {
		return nil, fmt.Errorf("Второй сервер не создал конфиг каскада")
	}
	p.secrets = append(p.secrets, password)
	removeRemote := func() { _, _ = mrttRequest(remote, target, "DELETE", "/api/clients/"+mrttPathEscape(password), "") }
	bundle, e := p.mrttBundle()
	if e != nil {
		removeRemote()
		return nil, e
	}
	machine, e := p.run(from, s, "uname -m", 30*time.Second, false)
	if e != nil {
		removeRemote()
		return nil, e
	}
	asset := ""
	switch strings.TrimSpace(machine) {
	case "x86_64", "amd64":
		asset = "mrtt-exit-client-amd64"
	case "aarch64", "arm64":
		asset = "mrtt-exit-client-arm64"
	default:
		removeRemote()
		return nil, fmt.Errorf("Каскад не поддерживает архитектуру %s", strings.TrimSpace(machine))
	}
	files, e := sftp.NewClient(from)
	if e != nil {
		removeRemote()
		return nil, e
	}
	defer files.Close()
	script := string(bundle["cascade.sh"])
	fail := func(cause error) (map[string]any, error) {
		_, _ = mrttCascadeRun(p, from, s, script, "CASCADE_OK", "client-down", id)
		removeRemote()
		return nil, cause
	}
	// SFTP gives no execute right and cannot write to a system folder without root.
	if e = uploadFile(files, "/tmp/.mrtt-upload-exit-client", bundle[asset]); e != nil {
		return fail(e)
	}
	if _, e = p.run(from, s, asRoot("install -m 0755 /tmp/.mrtt-upload-exit-client /usr/local/lib/mrtt-exit-client; status=$?; rm -f /tmp/.mrtt-upload-exit-client; exit $status"), 30*time.Second, false); e != nil {
		return fail(fmt.Errorf("Клиент каскада не установился"))
	}
	up, e := mrttCascadeRun(p, from, s, script, "CASCADE_CLIENT", "client-up", id, target["host"], target["peerPort"], password)
	if e != nil {
		return fail(e)
	}
	if _, e = mrttCascadeRun(p, from, s, script, "CASCADE_OK", "client-check", id); e != nil {
		return fail(e)
	}
	table := 0
	if len(up) > 1 {
		table, _ = strconv.Atoi(up[1])
	}
	title := target["name"]
	if title == "" {
		title = target["host"]
	}
	return map[string]any{"id": id, "name": title, "host": target["host"], "table": table, "remote_password": password}, nil
}

func mrttPathEscape(value string) string {
	var out strings.Builder
	for _, c := range []byte(value) {
		if c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '-' || c == '_' || c == '.' || c == '~' {
			out.WriteByte(c)
		} else {
			fmt.Fprintf(&out, "%%%02X", c)
		}
	}
	return out.String()
}

func (p *protocol) mrttAdmin(s map[string]string, raw string) (any, error) {
	var request struct {
		Action   string            `json:"action"`
		Method   string            `json:"method"`
		Path     string            `json:"path"`
		Body     string            `json:"body"`
		Target   map[string]string `json:"target"`
		ID       string            `json:"id"`
		Password string            `json:"remote_password"`
	}
	if e := json.Unmarshal([]byte(raw), &request); e != nil {
		return nil, fmt.Errorf("Некорректный запрос")
	}
	if e := validateMRTT(s, false); e != nil {
		return nil, e
	}
	client, e := p.dial(s, false)
	if e != nil {
		return nil, e
	}
	defer client.Close()
	switch request.Action {
	case "exit-add":
		if request.Target == nil || validateMRTT(request.Target, false) != nil {
			return nil, fmt.Errorf("Некорректный сервер выхода")
		}
		for _, k := range []string{"sshPassword", "key", "keyPassword", "sudoPassword", "password"} {
			p.secrets = append(p.secrets, request.Target[k])
		}
		return p.mrttAddExit(client, s, request.Target)
	case "exit-remove":
		if !regexp.MustCompile(`^[a-f0-9]{8}$`).MatchString(request.ID) {
			return nil, fmt.Errorf("Некорректный выход")
		}
		bundle, e := p.mrttBundle()
		if e != nil {
			return nil, e
		}
		if _, e = mrttCascadeRun(p, client, s, string(bundle["cascade.sh"]), "CASCADE_OK", "client-down", request.ID); e != nil {
			return nil, e
		}
		// An unavailable or already removed exit does not stop the removal here.
		if request.Target != nil && request.Password != "" && request.Target["hostKey"] != "" {
			if remote, e := p.dial(request.Target, false); e == nil {
				_, _ = mrttRequest(remote, request.Target, "DELETE", "/api/clients/"+mrttPathEscape(request.Password), "")
				remote.Close()
			}
		}
		return true, nil
	}
	if !strings.HasPrefix(request.Path, "/api/") || strings.ContainsAny(request.Path, " \r\n") {
		return nil, fmt.Errorf("Некорректный запрос")
	}
	switch request.Method {
	case "GET", "POST", "PUT", "DELETE":
	default:
		return nil, fmt.Errorf("Некорректный запрос")
	}
	return mrttRequest(client, s, request.Method, request.Path, request.Body)
}
