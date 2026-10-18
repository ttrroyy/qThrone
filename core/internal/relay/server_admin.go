package relay

import (
	"bytes"
	"crypto/sha256"
	"crypto/subtle"
	"crypto/tls"
	"crypto/x509"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"strings"
	"time"
)

// qwdttAdminPaths are the admin API calls of the original qWDTT app's AdminApiClient.
var qwdttAdminPaths = map[string]bool{
	"/admin/passwords": true, "/admin/passwords/update": true, "/admin/passwords/activate": true,
	"/admin/passwords/deactivate": true, "/admin/passwords/unbind-device": true, "/admin/passwords/delete": true,
}

type qwdttAdminCall struct {
	Path string            `json:"path"`
	Form map[string]string `json:"form"`
}

// qwdttAdminClient speaks HTTPS to the admin port with the certificate pinned to the
// fingerprint the installer printed (sha256/<base64 of the DER certificate>).
func qwdttAdminClient(pin string) (*http.Client, error) {
	if !strings.HasPrefix(pin, "sha256/") {
		return nil, errors.New("Отпечаток админ-панели не сохранён. Обновите сервер.")
	}
	expected, err := base64.StdEncoding.DecodeString(strings.TrimPrefix(pin, "sha256/"))
	if err != nil || len(expected) != sha256.Size {
		return nil, errors.New("Отпечаток админ-панели не сохранён. Обновите сервер.")
	}
	verify := func(raw [][]byte, _ [][]*x509.Certificate) error {
		if len(raw) == 0 {
			return errors.New("Сервер не прислал сертификат")
		}
		actual := sha256.Sum256(raw[0])
		if subtle.ConstantTimeCompare(actual[:], expected) != 1 {
			return errors.New("Сертификат сервера не совпадает с сохранённым")
		}
		return nil
	}
	// The pin replaces the CA and host name checks, as in the original app.
	transport := &http.Transport{TLSClientConfig: &tls.Config{InsecureSkipVerify: true, VerifyPeerCertificate: verify, MinVersion: tls.VersionTLS12},
		Proxy: nil, DialContext: (&net.Dialer{Timeout: 8 * time.Second}).DialContext, TLSHandshakeTimeout: 8 * time.Second}
	return &http.Client{Transport: transport, Timeout: 10 * time.Second}, nil
}

func (p *protocol) qwdttAdmin(s map[string]string, raw string) (any, error) {
	var call qwdttAdminCall
	if json.Unmarshal([]byte(raw), &call) != nil || !qwdttAdminPaths[call.Path] {
		return nil, errors.New("Некорректный запрос к серверу")
	}
	if s["adminToken"] == "" || !addressPattern.MatchString(s["host"]) {
		return nil, errors.New("Доступно после установки сервера из приложения")
	}
	adminPort, err := port(s, "adminPort")
	if err != nil {
		return nil, err
	}
	client, err := qwdttAdminClient(s["adminPin"])
	if err != nil {
		return nil, err
	}
	address := "https://" + net.JoinHostPort(strings.Trim(s["host"], "[]"), adminPort) + call.Path
	var request *http.Request
	if call.Form == nil {
		request, err = http.NewRequestWithContext(p.ctx, http.MethodGet, address, nil)
	} else {
		form := url.Values{}
		for key, value := range call.Form {
			form.Set(key, value)
		}
		request, err = http.NewRequestWithContext(p.ctx, http.MethodPost, address, strings.NewReader(form.Encode()))
		if request != nil {
			request.Header.Set("Content-Type", "application/x-www-form-urlencoded")
		}
	}
	if err != nil {
		return nil, err
	}
	request.Header.Set("Authorization", "Bearer "+s["adminToken"])
	response, err := client.Do(request)
	if err != nil {
		if p.ctx.Err() != nil {
			return nil, p.ctx.Err()
		}
		if strings.Contains(err.Error(), "Сертификат") {
			return nil, errors.New("Сертификат сервера не совпадает с сохранённым")
		}
		return nil, errors.New("Сервер не отвечает: проверьте адрес и порт админ-панели")
	}
	defer response.Body.Close()
	body, err := io.ReadAll(io.LimitReader(response.Body, 8*1024*1024))
	if err != nil {
		return nil, err
	}
	var value map[string]any
	if len(bytes.TrimSpace(body)) > 0 && json.Unmarshal(body, &value) != nil {
		return nil, fmt.Errorf("Сервер вернул не JSON (HTTP %d)", response.StatusCode)
	}
	if response.StatusCode < 200 || response.StatusCode > 299 {
		if message, _ := value["error"].(string); message != "" {
			return nil, errors.New(message)
		}
		return nil, fmt.Errorf("HTTP %d", response.StatusCode)
	}
	if value == nil {
		value = map[string]any{}
	}
	return value, nil
}

type wdttPlusAdminCall struct {
	Args    []string `json:"args"`
	Timeout int      `json:"timeout"`
}

// wdttPlusAdminCommand is the server's admin entry point; the request comes on stdin.
const wdttPlusAdminCommand = "/usr/local/bin/wdtt-server admin --config-dir /etc/wdtt --request-stdin"

// wdttPlusAdminInput is sudo's password (only for a non-root login) then the request line.
func wdttPlusAdminInput(s map[string]string, args []string) string {
	request, _ := json.Marshal(map[string]any{"main_password": s["password"], "args": args})
	input := ""
	if s["user"] != "root" {
		password := s["sudoPassword"]
		if password == "" {
			password = s["sshPassword"]
		}
		input = password + "\n"
	}
	return input + string(request) + "\n"
}

// wdttPlusAdminAnswer reads the server's last JSON line and its verdict.
func wdttPlusAdminAnswer(output string) (map[string]any, error) {
	lines := strings.Split(output, "\n")
	for i := len(lines) - 1; i >= 0; i-- {
		line := strings.TrimSpace(lines[i])
		if !strings.HasPrefix(line, "{") || !strings.HasSuffix(line, "}") {
			continue
		}
		var value map[string]any
		if json.Unmarshal([]byte(line), &value) != nil {
			continue
		}
		if ok, _ := value["ok"].(bool); !ok {
			message, _ := value["message"].(string)
			if message == "" {
				message = line
			}
			return nil, errors.New(wdttPlusFriendly(message))
		}
		return value, nil
	}
	if strings.TrimSpace(output) == "" {
		output = "сервер не вернул ответ"
	}
	return nil, errors.New(wdttPlusFriendly(output))
}

func wdttPlusFriendly(raw string) string {
	lower := strings.ToLower(raw)
	switch {
	case strings.Contains(lower, "no_passwords_json"):
		return "На сервере не найдена база WDTT Plus. Сначала выполните деплой."
	case strings.Contains(lower, "admin_socket_unavailable"):
		return "Сервер не поддерживает управление без перезапуска. Обновите сервер."
	case strings.Contains(lower, "main_password") || strings.Contains(lower, "unauthorized"):
		return "Главный пароль не подходит к серверу."
	}
	lines := strings.Split(strings.TrimSpace(raw), "\n")
	last := strings.TrimSpace(lines[len(lines)-1])
	if len([]rune(last)) > 300 {
		last = string([]rune(last)[:300])
	}
	return last
}

func (p *protocol) wdttPlusAdmin(s map[string]string, raw string) (any, error) {
	var call wdttPlusAdminCall
	if json.Unmarshal([]byte(raw), &call) != nil || len(call.Args) == 0 {
		return nil, errors.New("Некорректный запрос к серверу")
	}
	if s["password"] == "" {
		return nil, errors.New("Доступно после установки сервера из приложения")
	}
	timeout := 45 * time.Second
	if call.Timeout > 0 && call.Timeout <= 600 {
		timeout = time.Duration(call.Timeout) * time.Second
	}
	client, err := p.dial(s, false)
	if err != nil {
		return nil, err
	}
	defer client.Close()
	output, err := p.runInput(client, asRootStdin(wdttPlusAdminCommand)+" 2>&1", wdttPlusAdminInput(s, call.Args), timeout)
	if p.ctx.Err() != nil {
		return nil, p.ctx.Err()
	}
	answer, answerErr := wdttPlusAdminAnswer(output)
	if answerErr != nil && err != nil && strings.TrimSpace(output) == "" {
		return nil, err
	}
	return answer, answerErr
}

// asRootStdin runs a command that reads stdin as root; for a non-root login sudo takes the
// first stdin line as its password.
func asRootStdin(command string) string {
	return "if [ \"$(id -u)\" = 0 ]; then " + command + "; else sudo -S -p '' " + command + "; fi"
}
