package relay

import (
	"encoding/json"
	"fmt"
	"regexp"
	"strings"

	"github.com/pkg/sftp"
	"golang.org/x/crypto/ssh"
)

var wdttPlusStaging = regexp.MustCompile(`^/tmp/wdtt-plus-deploy\.[A-Za-z0-9]+$`)
var wdttPlusSuccess = regexp.MustCompile(`(?m)^WDTT_INSTALL_RESULT=success\s*$`)

// deployWdttPlus deploys as WDTT Plus's app does: the pinned official APK's deploy.sh and
// server go to a private staging folder (mktemp, 700); a new server also gets passwords.json
// with the main password, an existing one is updated in place and keeps its data.
// WDTT_INSTALL_RESULT=success, the active wdtt service and its admin socket confirm the install.
func (p *protocol) deployWdttPlus(client *ssh.Client, s map[string]string, install bool, machine string, exec func(string) (string, error)) error {
	if install && machine != "x86_64" && machine != "amd64" {
		return fmt.Errorf("Официальный сервер WDTT Plus собран только для x86_64")
	}
	p.emit("progress", "Загрузка официального установщика", 10)
	apk, e := artifact(p.ctx, "wdttplus")
	if e != nil {
		return e
	}
	out, e := exec("mktemp -d /tmp/wdtt-plus-deploy.XXXXXX")
	if e != nil {
		return e
	}
	lines := strings.Split(strings.TrimSpace(out), "\n")
	staging := strings.TrimSpace(lines[len(lines)-1])
	if !wdttPlusStaging.MatchString(staging) {
		return fmt.Errorf("Не удалось подготовить временный каталог")
	}
	defer exec("rm -rf " + quote(staging))
	if _, e = exec("chmod 700 " + quote(staging)); e != nil {
		return e
	}
	existingOut, e := exec(asRoot("[ -s /etc/wdtt/passwords.json ] && echo WDTT_DB=1 || echo WDTT_DB=0"))
	if e != nil {
		return e
	}
	existing := strings.Contains(existingOut, "WDTT_DB=1")
	p.emit("progress", "Передача файлов на VPS", 30)
	script, e := zipEntry(apk, "assets/deploy.sh")
	if e != nil {
		return e
	}
	uploads := map[string][]byte{staging + "/deploy.sh": script}
	if install {
		if uploads[staging+"/server"], e = zipEntry(apk, "assets/server"); e != nil {
			return e
		}
		if !existing {
			uploads[staging+"/passwords.json"] = wdttPlusDatabase(s)
		}
	}
	files, e := sftp.NewClient(client)
	if e != nil {
		return e
	}
	for path, data := range uploads {
		if e = uploadFile(files, path, data); e != nil {
			files.Close()
			return e
		}
	}
	files.Close()
	step := "Установка и запуск сервиса"
	if !install {
		step = "Удаление"
	} else if existing {
		step = "Обновление с сохранением данных"
	}
	p.emit("progress", step, 45)
	output, e := exec(asRoot(wdttPlusCommand(s, staging, install, existing)))
	if e != nil {
		return fmt.Errorf("Установщик завершился с ошибкой: %w", e)
	}
	if !install {
		p.emit("config", "", "")
		p.emit("progress", "Готово", 100)
		return nil
	}
	if !wdttPlusSuccess.MatchString(output) {
		return fmt.Errorf("Установщик не подтвердил запуск сервиса")
	}
	verify, e := exec(asRoot(`sleep 2; printf 'SERVICE=%s\n' "$(systemctl is-active wdtt 2>/dev/null || true)"; printf 'ADMIN_SOCKET=%s\n' "$([ -S /run/wdtt/admin.sock ] && echo 1 || echo 0)"`))
	if e != nil {
		return e
	}
	if !strings.Contains(verify, "SERVICE=active") {
		return fmt.Errorf("Служба wdtt не запустилась")
	}
	if !strings.Contains(verify, "ADMIN_SOCKET=1") {
		return fmt.Errorf("Сервер не открыл канал управления")
	}
	p.emit("config", "", wdttPlusLink(s))
	p.emit("progress", "Готово", 100)
	return nil
}

// wdttPlusDatabase is the passwords.json a fresh server starts from, as the app writes it.
func wdttPlusDatabase(s map[string]string) []byte {
	data, _ := json.Marshal(map[string]any{
		"main_password": s["password"], "admin_id": "", "bot_token": "", "passwords": map[string]any{}, "devices": map[string]any{},
		"dns": s["dns"], "default_ports": s["peerPort"] + "," + s["wgPort"] + "," + s["localPort"], "max_passwords": 50,
	})
	return data
}

// wdttPlusCommand runs the staged deploy.sh with the app's environment.
func wdttPlusCommand(s map[string]string, staging string, install, existing bool) string {
	if !install {
		return "bash " + quote(staging+"/deploy.sh") + " uninstall"
	}
	mode, preserve := "fresh", "0"
	if existing {
		mode, preserve = "preserve", "1"
	}
	command := "env WDTT_DTLS_PORT=" + s["peerPort"] + " WDTT_WG_PORT=" + s["wgPort"] + " WDTT_SSH_PORT=" + s["sshPort"] +
		" WDTT_INSTALL_MODE=" + mode + " WDTT_PRESERVE_DATA=" + preserve
	if s["dns"] != "" {
		command += " WDTT_DNS=" + quote(s["dns"])
	}
	command += " WDTT_STAGED_SERVER=" + quote(staging+"/server")
	if !existing {
		command += " WDTT_STAGED_DB=" + quote(staging+"/passwords.json")
	}
	return command + " bash " + quote(staging+"/deploy.sh")
}

// percentEncode is QUrl::toPercentEncoding: everything but A-Z a-z 0-9 - . _ ~ is escaped,
// so the link is the one the WDTT Plus profile of qThrone exports.
func percentEncode(value string) string {
	var b strings.Builder
	for _, c := range []byte(value) {
		if 'a' <= c && c <= 'z' || 'A' <= c && c <= 'Z' || '0' <= c && c <= '9' || strings.IndexByte("-._~", c) >= 0 {
			b.WriteByte(c)
		} else {
			fmt.Fprintf(&b, "%%%02X", c)
		}
	}
	return b.String()
}

// wdttPlusLink is the wdtt://connect link of a deployed WDTT Plus server; no device id.
func wdttPlusLink(s map[string]string) string {
	name := s["name"]
	if name == "" {
		name = "WDTT Plus"
	}
	return "wdtt://connect?v=1&host=" + percentEncode(s["host"]) + "&dtls=" + s["peerPort"] + "&wg=" + s["wgPort"] +
		"&local=" + s["localPort"] + "&password=" + percentEncode(s["password"]) + "&hashes=&name=" + percentEncode(name)
}
