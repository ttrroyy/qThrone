package relay

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"regexp"
	"strings"
	"time"

	"github.com/pkg/sftp"
	"golang.org/x/crypto/ssh"
)

// FreeTurn's own installer, pinned to the release of the client Rawline carries: it answers in JSON and sets up the server.
const (
	freeturnVersion   = "v4.1.2"
	freeturnInstaller = "https://github.com/hackdiaz-dev/free-turn-proxy/releases/download/v4.1.2/install.sh"
	freeturnSHA256    = "91fcea2aa5558d019431da31aa1f32baa0ffb943600529810e073ffa22f83100"
	freeturnScript    = "/opt/free-turn-proxy/install-rawline.sh"
)

var (
	freeturnClientName = regexp.MustCompile(`^[A-Za-z0-9][A-Za-z0-9._-]{0,31}$`)
	freeturnConnect    = regexp.MustCompile(`^[A-Za-z0-9.:\[\]-]+:[0-9]{1,5}$`)
)

func validateFreeturn(s map[string]string, install bool) error {
	if !addressPattern.MatchString(s["host"]) || !userPattern.MatchString(s["user"]) {
		return fmt.Errorf("Укажите IP/домен и логин SSH")
	}
	for _, k := range []string{"sshPort", "peerPort", "wgPort"} {
		if _, e := port(s, k); e != nil {
			return e
		}
	}
	if s["auth"] == "key" && s["key"] == "" {
		return fmt.Errorf("Загрузите SSH-ключ")
	}
	if s["auth"] != "key" && s["sshPassword"] == "" {
		return fmt.Errorf("Введите пароль SSH")
	}
	if install && s["ftBackend"] == "external" && !freeturnConnect.MatchString(s["ftConnect"]) {
		return fmt.Errorf("Укажите адрес VPN на сервере: хост:порт")
	}
	return nil
}

func freeturnApplyArguments(s map[string]string) []string {
	or := func(key, fallback string) string {
		if s[key] != "" {
			return s[key]
		}
		return fallback
	}
	arguments := []string{"apply", "--method=" + or("ftMethod", "docker"), "--backend=" + or("ftBackend", "new"), "--host=" + s["host"], "--listen-port=" + s["peerPort"]}
	if s["ftBackend"] == "external" {
		arguments = append(arguments, "--connect="+s["ftConnect"], "--mode="+or("ftMode", "udp"))
	} else {
		arguments = append(arguments, "--wg-port="+s["wgPort"])
	}
	return append(arguments, "--obf-profile="+or("ftObf", "rtpopus3"), "--version="+freeturnVersion)
}

// freeturnAnswer is the installer's one JSON line; a refusal becomes an error with its own message.
func freeturnAnswer(output string) (map[string]any, error) {
	lines := strings.Split(output, "\n")
	for i := len(lines) - 1; i >= 0; i-- {
		line := strings.TrimSpace(lines[i])
		if !strings.HasPrefix(line, `{"proto"`) {
			continue
		}
		var answer struct {
			Result string         `json:"result"`
			Msg    string         `json:"msg"`
			Code   string         `json:"code"`
			Data   map[string]any `json:"data"`
		}
		if e := json.Unmarshal([]byte(line), &answer); e != nil {
			return nil, fmt.Errorf("Не удалось прочитать ответ установщика FreeTurn")
		}
		if answer.Result != "ok" {
			if answer.Msg != "" {
				return nil, fmt.Errorf("%s", answer.Msg)
			}
			return nil, fmt.Errorf("Ошибка установщика FreeTurn: %s", answer.Code)
		}
		return answer.Data, nil
	}
	return nil, fmt.Errorf("Установщик FreeTurn не вернул ответ")
}

func freeturnDownload(ctx context.Context) ([]byte, error) {
	request, e := http.NewRequestWithContext(ctx, http.MethodGet, freeturnInstaller, nil)
	if e != nil {
		return nil, e
	}
	response, e := (&http.Client{Timeout: 2 * time.Minute}).Do(request)
	if e != nil {
		return nil, fmt.Errorf("Не удалось загрузить установщик FreeTurn: %w", e)
	}
	defer response.Body.Close()
	data, e := io.ReadAll(io.LimitReader(response.Body, 4<<20))
	if e != nil {
		return nil, e
	}
	sum := sha256.Sum256(data)
	if hex.EncodeToString(sum[:]) != freeturnSHA256 {
		return nil, fmt.Errorf("Контрольная сумма установщика FreeTurn не совпала")
	}
	return data, nil
}

func (p *protocol) deployFreeturn(client *ssh.Client, s map[string]string, install bool) error {
	p.emit("progress", "Загрузка установщика FreeTurn", 10)
	script, e := freeturnDownload(p.ctx)
	if e != nil {
		return e
	}
	files, e := sftp.NewClient(client)
	if e != nil {
		return e
	}
	defer files.Close()
	if e = uploadFile(files, "/tmp/.freeturn-install.sh", script); e != nil {
		return e
	}
	arguments := []string{"uninstall", "--target=all", "--purge"}
	if install {
		arguments = freeturnApplyArguments(s)
	}
	command := "install -D -m 0700 /tmp/.freeturn-install.sh " + quote(freeturnScript) + " && rm -f /tmp/.freeturn-install.sh && bash " + quote(freeturnScript)
	for _, argument := range arguments {
		command += " " + quote(argument)
	}
	p.emit("progress", "Выполнение установщика", 30)
	// The answer holds the client's keys, so it stays out of the console.
	output, e := p.run(client, s, asRoot(command), 30*time.Minute, false)
	data, answerError := freeturnAnswer(output)
	if answerError != nil {
		return answerError
	}
	if e != nil {
		return fmt.Errorf("Установщик завершился с ошибкой: %w", e)
	}
	link := ""
	if install {
		owner, _ := data["owner"].(map[string]any)
		link, _ = owner["link"].(string)
		if link == "" {
			return fmt.Errorf("Установщик не вернул ссылку подключения")
		}
	}
	p.emit("config", "", link)
	p.emit("progress", "Готово", 100)
	return nil
}

// freeturnAdmin lists, adds, shows and removes the clients of a deployed server. value is {"action","name"}.
func (p *protocol) freeturnAdmin(s map[string]string, value string) (any, error) {
	var call struct {
		Action string `json:"action"`
		Name   string `json:"name"`
	}
	if e := json.Unmarshal([]byte(value), &call); e != nil {
		return nil, e
	}
	command := map[string]string{"list": "client-list", "add": "client-add", "show": "client-conf", "remove": "client-remove"}[call.Action]
	if command == "" {
		return nil, fmt.Errorf("Неизвестная команда")
	}
	arguments := "bash " + quote(freeturnScript) + " " + command
	if call.Action != "list" {
		if !freeturnClientName.MatchString(call.Name) {
			return nil, fmt.Errorf("Имя: латинские буквы, цифры, точка, дефис, до 32 символов")
		}
		arguments += " " + quote("--name="+call.Name)
	}
	client, e := p.dial(s, false)
	if e != nil {
		return nil, e
	}
	defer client.Close()
	output, _ := p.run(client, s, asRoot(arguments), time.Minute, false)
	return freeturnAnswer(output)
}
