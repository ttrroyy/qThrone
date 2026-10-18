package relay

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"regexp"
	"strings"
	"time"

	"github.com/pkg/sftp"
	"golang.org/x/crypto/ssh"
)

var wdttPlusStaging = regexp.MustCompile(`^/tmp/wdtt-plus-deploy\.[A-Za-z0-9]+$`)
var wdttPlusSuccess = regexp.MustCompile(`(?m)^WDTT_INSTALL_RESULT=success\s*$`)
var wdttPlusVersion = regexp.MustCompile(`(?m)^SERVER_VERSION=([0-9][0-9A-Za-z._-]*)\s*$`)
var wdttPlusSHA = regexp.MustCompile(`(?m)^SERVER_SHA256=([0-9a-fA-F]{64})\s*$`)

// wdttPlusOwnershipProbe is WDTT Plus's standaloneInstallerOwnershipProbeScript: who manages the
// installation under prefix ("" on a server). Its conditions are all inside if, so set -e is moot.
func wdttPlusOwnershipProbe(prefix string) string {
	return `root_prefix=` + quote(strings.TrimRight(prefix, "/")) + `
ownership="$root_prefix/var/lib/wdtt-server-installer/ownership"
unit="$root_prefix/etc/systemd/system/wdtt.service"
binary="$root_prefix/usr/local/bin/wdtt-server"
config="$root_prefix/etc/wdtt"
passwords="$root_prefix/etc/wdtt/passwords.json"
wg_keys="$root_prefix/etc/wdtt/wg-keys.dat"
preserved_marker="$root_prefix/etc/wdtt/.android-deploy-preserved"
marker='Managed by WDTT Plus standalone server installer'
if [ -f "$ownership" ] && [ ! -L "$ownership" ] &&
   [ -f "$unit" ] && [ ! -L "$unit" ] &&
   grep -Fqx "$marker" "$ownership" &&
   grep -Fqx "# $marker" "$unit"; then
  printf 'WDTT_STANDALONE_MANAGED=1\n'
else
  printf 'WDTT_STANDALONE_MANAGED=0\n'
fi
android_marker_present=0
if [ -f "$unit" ] && [ ! -L "$unit" ] &&
   grep -Fqx '# Managed by WDTT Plus Android deploy' "$unit" &&
   grep -Fqx '# WDTT deploy compatibility: 1' "$unit"; then
  android_marker_present=1
fi
android_deploy_managed=0
if [ "$android_marker_present" = "1" ] &&
   [ -f "$binary" ] && [ ! -L "$binary" ] && [ -x "$binary" ] &&
   [ -d "$config" ] && [ ! -L "$config" ] &&
   [ -f "$passwords" ] && [ ! -L "$passwords" ] && [ -s "$passwords" ] &&
   [ -f "$wg_keys" ] && [ ! -L "$wg_keys" ] && [ -s "$wg_keys" ] &&
   grep -Eq '^ExecStart=/usr/local/bin/wdtt-server([[:space:]]|$)' "$unit" &&
   grep -Eq '(^|[[:space:]])-config-dir[[:space:]]+/etc/wdtt([[:space:]]|$)' "$unit" &&
   grep -Fq 'wdtt0' "$unit"; then
  android_deploy_managed=1
  printf 'WDTT_ANDROID_DEPLOY_MANAGED=1\n'
else
  printf 'WDTT_ANDROID_DEPLOY_MANAGED=0\n'
fi
if [ -f "$unit" ] && [ ! -L "$unit" ] &&
   [ -f "$binary" ] && [ ! -L "$binary" ] && [ -x "$binary" ] &&
   [ -d "$config" ] && [ ! -L "$config" ] &&
   [ -f "$passwords" ] && [ ! -L "$passwords" ] && [ -s "$passwords" ] &&
   [ -f "$wg_keys" ] && [ ! -L "$wg_keys" ] && [ -s "$wg_keys" ] &&
   grep -Eq '^ExecStart=/usr/local/bin/wdtt-server([[:space:]]|$)' "$unit" &&
   grep -Eq '(^|[[:space:]])-config-dir[[:space:]]+/etc/wdtt([[:space:]]|$)' "$unit" &&
   grep -Fq 'wdtt0' "$unit"; then
  printf 'WDTT_LEGACY_ANDROID_DEPLOY_CANDIDATE=1\n'
else
  printf 'WDTT_LEGACY_ANDROID_DEPLOY_CANDIDATE=0\n'
fi
preserved_extra=''
if [ -d "$config" ] && [ ! -L "$config" ]; then
  preserved_extra="$(find "$config" -mindepth 1 -maxdepth 1 \
    ! -path "$passwords" ! -path "$wg_keys" ! -path "$preserved_marker" \
    -print -quit 2>/dev/null || true)"
fi
if [ ! -e "$ownership" ] && [ ! -L "$ownership" ] &&
   [ ! -e "$unit" ] && [ ! -L "$unit" ] &&
   [ ! -e "$binary" ] && [ ! -L "$binary" ] &&
   [ -d "$config" ] && [ ! -L "$config" ] &&
   [ -f "$passwords" ] && [ ! -L "$passwords" ] && [ -s "$passwords" ] &&
   [ -f "$wg_keys" ] && [ ! -L "$wg_keys" ] && [ -s "$wg_keys" ] &&
   [ "$(stat -c '%a' "$config" 2>/dev/null || true)" = "700" ] &&
   [ "$(stat -c '%a' "$passwords" 2>/dev/null || true)" = "600" ] &&
   [ "$(stat -c '%a' "$wg_keys" 2>/dev/null || true)" = "600" ] &&
   { { [ -f "$preserved_marker" ] && [ ! -L "$preserved_marker" ] &&
         [ "$(cat "$preserved_marker" 2>/dev/null)" = 'Preserved by WDTT Plus Android deploy' ]; } ||
     { [ ! -e "$preserved_marker" ] && [ -z "$preserved_extra" ]; }; }; then
  printf 'WDTT_ANDROID_DATA_PRESERVED=1\n'
else
  printf 'WDTT_ANDROID_DATA_PRESERVED=0\n'
fi
incomplete_extra=''
if [ -d "$config" ] && [ ! -L "$config" ]; then
  incomplete_extra="$(find "$config" -mindepth 1 -maxdepth 1 ! -path "$passwords" -print -quit 2>/dev/null || true)"
fi
if [ "$android_deploy_managed" = "0" ] &&
   [ "$android_marker_present" = "1" ] &&
   [ ! -e "$ownership" ] && [ ! -L "$ownership" ] &&
   { { [ ! -e "$binary" ] && [ ! -L "$binary" ]; } ||
     { [ -f "$binary" ] && [ ! -L "$binary" ]; }; } &&
   { { [ ! -e "$config" ] && [ ! -L "$config" ]; } ||
     { [ -d "$config" ] && [ ! -L "$config" ]; }; } &&
   { { [ ! -e "$passwords" ] && [ ! -L "$passwords" ]; } ||
     { [ -f "$passwords" ] && [ ! -L "$passwords" ]; }; } &&
   { { [ ! -e "$wg_keys" ] && [ ! -L "$wg_keys" ]; } ||
     { [ -f "$wg_keys" ] && [ ! -L "$wg_keys" ]; }; }; then
  printf 'WDTT_INCOMPLETE_ANDROID_DEPLOY_CANDIDATE=1\n'
elif [ ! -e "$ownership" ] && [ ! -L "$ownership" ] &&
   [ ! -e "$unit" ] && [ ! -L "$unit" ] &&
   [ ! -e "$binary" ] && [ ! -L "$binary" ] &&
   [ -d "$config" ] && [ ! -L "$config" ] &&
   [ -f "$passwords" ] && [ ! -L "$passwords" ] && [ -s "$passwords" ] &&
   [ ! -e "$wg_keys" ] && [ -z "$incomplete_extra" ] &&
   [ "$(stat -c '%a' "$config" 2>/dev/null || true)" = "700" ] &&
   [ "$(stat -c '%a' "$passwords" 2>/dev/null || true)" = "600" ]; then
  printf 'WDTT_INCOMPLETE_ANDROID_DEPLOY_CANDIDATE=1\n'
else
  printf 'WDTT_INCOMPLETE_ANDROID_DEPLOY_CANDIDATE=0\n'
fi
if [ -f "$unit" ] || [ -f "$binary" ] || [ -d "$config" ] ||
   [ -f "$passwords" ] || [ -f "$wg_keys" ]; then
  printf 'WDTT_INSTALL_TRACE=1\n'
else
  printf 'WDTT_INSTALL_TRACE=0\n'
fi
# Not in the original: data deploy.sh uninstall keeps (qThrone 1.5 and 1.5.1 removed the service
# so): no unit, server or installer, both data files regular and non-empty, no marker, no links.
# The original calls it an unknown install and refuses to deploy again.
if [ ! -e "$ownership" ] && [ ! -L "$ownership" ] &&
   [ ! -e "$unit" ] && [ ! -L "$unit" ] &&
   [ ! -e "$binary" ] && [ ! -L "$binary" ] &&
   [ -d "$config" ] && [ ! -L "$config" ] &&
   [ -f "$passwords" ] && [ ! -L "$passwords" ] && [ -s "$passwords" ] &&
   [ -f "$wg_keys" ] && [ ! -L "$wg_keys" ] && [ -s "$wg_keys" ] &&
   [ ! -e "$preserved_marker" ] && [ ! -L "$preserved_marker" ] &&
   [ -z "$(find "$config" -mindepth 1 ! -type f ! -type d -print -quit 2>/dev/null || echo unreadable)" ]; then
  printf 'WDTT_UNINSTALLED_DATA=1\n'
else
  printf 'WDTT_UNINSTALLED_DATA=0\n'
fi`
}

type wdttPlusOwnership int

const (
	wdttPlusNoInstall wdttPlusOwnership = iota
	wdttPlusStandalone
	wdttPlusAndroidDeploy
	wdttPlusLegacyDeploy
	wdttPlusPreservedData
	wdttPlusIncomplete
	wdttPlusUnknown
	wdttPlusUninstalledData // left by deploy.sh uninstall; adopted with wdttPlusAdoptKept
)

// wdttPlusOwnershipFrom is deploymentOwnershipFromProbe: an incomplete answer stops the operation.
func wdttPlusOwnershipFrom(output string) (wdttPlusOwnership, error) {
	names := []string{"WDTT_STANDALONE_MANAGED", "WDTT_ANDROID_DEPLOY_MANAGED", "WDTT_LEGACY_ANDROID_DEPLOY_CANDIDATE",
		"WDTT_ANDROID_DATA_PRESERVED", "WDTT_INCOMPLETE_ANDROID_DEPLOY_CANDIDATE", "WDTT_INSTALL_TRACE"}
	for _, name := range names {
		if v := wdttPlusMarker(output, name); v != "0" && v != "1" {
			return 0, fmt.Errorf("Не удалось безопасно проверить владельца установки WDTT Plus. Изменяющая операция остановлена")
		}
	}
	for i, ownership := range []wdttPlusOwnership{wdttPlusStandalone, wdttPlusAndroidDeploy, wdttPlusLegacyDeploy, wdttPlusPreservedData, wdttPlusIncomplete} {
		if wdttPlusMarker(output, names[i]) == "1" {
			return ownership, nil
		}
	}
	if wdttPlusMarker(output, "WDTT_INSTALL_TRACE") == "0" {
		return wdttPlusNoInstall, nil
	}
	if wdttPlusMarker(output, "WDTT_UNINSTALLED_DATA") == "1" {
		return wdttPlusUninstalledData, nil
	}
	return wdttPlusUnknown, nil
}

const wdttPlusStandaloneRefusal = "Сервер управляется отдельным установщиком WDTT Plus (server-installer). qThrone не меняет такую установку: используйте server-installer/install.sh на VPS"

// wdttPlusMayInstall is assertDeploymentMayBeUpdated for the modes qThrone offers: a clean server
// gets a fresh install, a confirmed WDTT Plus deploy (or its kept data) a preserving update. The
// original's reset of an incomplete install deletes the data, so qThrone leaves it to the user.
func wdttPlusMayInstall(ownership wdttPlusOwnership) (preserve bool, e error) {
	switch ownership {
	case wdttPlusNoInstall:
		return false, nil
	case wdttPlusAndroidDeploy, wdttPlusLegacyDeploy, wdttPlusPreservedData, wdttPlusUninstalledData:
		return true, nil
	case wdttPlusStandalone:
		return false, fmt.Errorf(wdttPlusStandaloneRefusal)
	case wdttPlusIncomplete:
		return false, fmt.Errorf("На сервере неполная установка WDTT Plus. Обновить её с сохранением нельзя, а сброс удалит данные: " +
			"сбросьте сервер в приложении WDTT Plus или удалите остатки установки на VPS вручную и повторите деплой")
	}
	return false, fmt.Errorf("На сервере есть неизвестная установка WDTT без признаков деплоя WDTT Plus. Изменяющая операция остановлена")
}

// wdttPlusMayUninstall is assertAndroidDeployMayManageServer for removal.
func wdttPlusMayUninstall(ownership wdttPlusOwnership) error {
	switch ownership {
	case wdttPlusAndroidDeploy, wdttPlusPreservedData, wdttPlusUninstalledData:
		return nil
	case wdttPlusStandalone:
		return fmt.Errorf(wdttPlusStandaloneRefusal)
	case wdttPlusLegacyDeploy:
		return fmt.Errorf("Старая установка WDTT Plus без метки не допускает удаления. Сначала обновите сервер деплоем")
	}
	return fmt.Errorf("Не удалось безопасно подтвердить, что установка принадлежит деплою WDTT Plus. Удаление остановлено")
}

// wdttPlusMarkPreserved is WDTT Plus's markPreservedAndroidData: the marker deploy.sh accepts for
// data kept after the service was removed. Each check exits on its own: set -e ignores a failure
// on the left of &&, which lets the original mark data without its files.
const wdttPlusMarkPreserved = "set -e; " +
	"{ [ -d /etc/wdtt ] && [ ! -L /etc/wdtt ]; } || exit 1; " +
	"{ [ -f /etc/wdtt/passwords.json ] && [ ! -L /etc/wdtt/passwords.json ]; } || exit 1; " +
	"{ [ -f /etc/wdtt/wg-keys.dat ] && [ ! -L /etc/wdtt/wg-keys.dat ]; } || exit 1; " +
	"printf '%s\\n' 'Preserved by WDTT Plus Android deploy' > /etc/wdtt/.android-deploy-preserved.new; " +
	"chmod 600 /etc/wdtt/.android-deploy-preserved.new; " +
	"mv -f /etc/wdtt/.android-deploy-preserved.new /etc/wdtt/.android-deploy-preserved; " +
	"echo WDTT_PRESERVED_MARKER=ready"

// wdttPlusAdoptKept secures the data deploy.sh uninstall kept as WDTT Plus keeps its own
// (700 and 600) and marks it, so it is then the original's preserved data.
const wdttPlusAdoptKept = "{ [ -d /etc/wdtt ] && [ ! -L /etc/wdtt ]; } || exit 1; chmod 700 /etc/wdtt; " +
	"{ [ -f /etc/wdtt/passwords.json ] && [ ! -L /etc/wdtt/passwords.json ]; } || exit 1; chmod 600 /etc/wdtt/passwords.json; " +
	"{ [ -f /etc/wdtt/wg-keys.dat ] && [ ! -L /etc/wdtt/wg-keys.dat ]; } || exit 1; chmod 600 /etc/wdtt/wg-keys.dat; " + wdttPlusMarkPreserved

// wdttPlusMarkFor marks the kept data of an ownership before deploy.sh or the removal runs.
func wdttPlusMarkFor(ownership wdttPlusOwnership) string {
	if ownership == wdttPlusUninstalledData {
		return wdttPlusAdoptKept
	}
	return wdttPlusMarkPreserved
}

// wdttPlusClearStaleMarker runs before updating a live install. deploy.sh drops the marker only
// once the service starts and refuses it next to a unit, so after a failed start (or an
// interrupted removal) the original's next update stops; the marker is not needed there.
const wdttPlusClearStaleMarker = "rm -f /etc/wdtt/.android-deploy-preserved"

// wdttPlusReadDB is readRemotePasswordsJson: the database base64-encoded, size-limited and never a link.
const wdttPlusReadDB = `path=/etc/wdtt/passwords.json; if [ -e "$path" ]; then ` +
	`[ -f "$path" ] && [ ! -L "$path" ] || { echo WDTT_ERROR=unsafe_passwords_json; exit 2; }; ` +
	`size=$(stat -c %s "$path" 2>/dev/null || echo 0); ` +
	`[ "$size" -gt 0 ] && [ "$size" -le 5000000 ] || { echo WDTT_ERROR=bad_passwords_json_size; exit 2; }; ` +
	`printf 'WDTT_DB_B64='; base64 "$path" | tr -d '\n'; printf '\n'; fi`

// wdttPlusParseRead turns wdttPlusReadDB's output into the database, nil when there is none.
func wdttPlusParseRead(output string) (wdttPlusDB, error) {
	if reason := wdttPlusMarker(output, "WDTT_ERROR"); reason != "" {
		return nil, fmt.Errorf("Сервер отклонил passwords.json: %s", reason)
	}
	encoded := wdttPlusMarker(output, "WDTT_DB_B64")
	if encoded == "" {
		return nil, nil
	}
	data, e := base64.StdEncoding.DecodeString(encoded)
	if e != nil {
		return nil, fmt.Errorf("passwords.json на сервере прочитан не полностью")
	}
	return wdttPlusParseDB(string(data))
}

const wdttPlusVerify = `sleep 2; printf 'BINARY=%s\n' "$([ -x /usr/local/bin/wdtt-server ] && echo 1 || echo 0)"; ` +
	`printf 'CONFIG=%s\n' "$([ -d /etc/wdtt ] && echo 1 || echo 0)"; ` +
	`printf 'SERVICE=%s\n' "$(systemctl is-active wdtt 2>/dev/null || true)"; ` +
	`printf 'ADMIN_SOCKET=%s\n' "$([ -S /run/wdtt/admin.sock ] && echo 1 || echo 0)"; ` +
	`printf 'SERVER_SHA256=%s\n' "$(sha256sum /usr/local/bin/wdtt-server 2>/dev/null | awk '{print $1}')"; ` +
	`printf 'SERVER_VERSION=%s\n' "$(/usr/local/bin/wdtt-server --version 2>/dev/null | head -n 1)"`

// wdttPlusVerifyFailures lists what the installed server lacks, as performDeploy checks it.
func wdttPlusVerifyFailures(output, serverSHA string) []string {
	var missing []string
	for _, c := range []struct{ line, what string }{{"BINARY=1", "бинарник"}, {"CONFIG=1", "конфиг /etc/wdtt"},
		{"SERVICE=active", "служба wdtt"}, {"ADMIN_SOCKET=1", "канал управления"}} {
		if !regexp.MustCompile(`(?m)^` + c.line + `\s*$`).MatchString(output) {
			missing = append(missing, c.what)
		}
	}
	if m := wdttPlusSHA.FindStringSubmatch(output); m == nil || !strings.EqualFold(m[1], serverSHA) {
		missing = append(missing, "актуальная версия бинарника")
	}
	if !wdttPlusVersion.MatchString(output) {
		missing = append(missing, "версия серверной части")
	}
	return missing
}

// deployWdttPlus deploys as WDTT Plus's app does (performDeploy, performUninstall): root access and
// the installation's owner are checked first; a preserving update validates passwords.json,
// backs up the stopped service, writes the database with the profile's settings and is rolled
// back on any failure until the new server, its data and its clients are verified.
func (p *protocol) deployWdttPlus(client *ssh.Client, s map[string]string, install bool, machine string, exec func(string) (string, error)) (err error) {
	if install && machine != "x86_64" && machine != "amd64" {
		return fmt.Errorf("Официальный сервер WDTT Plus собран только для x86_64")
	}
	quiet := func(command string) (string, error) { return p.run(client, s, command, 3*time.Minute, false) }
	p.emit("progress", "Проверка прав администратора", 7)
	if out, e := exec(asRoot(`printf 'WDTT_ROOT_ACCESS=ok\n'`)); e != nil || wdttPlusMarker(out, "WDTT_ROOT_ACCESS") != "ok" {
		if p.ctx.Err() != nil {
			return p.ctx.Err()
		}
		return fmt.Errorf("Сервер не подтвердил права администратора: нужен вход под root или пароль sudo. Деплой не запущен")
	}
	p.emit("progress", "Проверка владельца установки", 8)
	probe, e := exec(asRoot(wdttPlusOwnershipProbe("")))
	if e != nil {
		return e
	}
	ownership, e := wdttPlusOwnershipFrom(probe)
	if e != nil {
		return e
	}
	if !install {
		return p.uninstallWdttPlus(s, ownership, exec)
	}
	preserve, e := wdttPlusMayInstall(ownership)
	if e != nil {
		return e
	}
	p.emit("progress", "Загрузка официального установщика", 10)
	apk, e := artifact(p.ctx, "wdttplus")
	if e != nil {
		return e
	}
	script, e := zipEntry(apk, "assets/deploy.sh")
	if e != nil {
		return e
	}
	server, e := zipEntry(apk, "assets/server")
	if e != nil {
		return e
	}
	sum := sha256.Sum256(server)
	serverSHA := hex.EncodeToString(sum[:])

	var before wdttPlusDB
	attempt, verified := "", false
	if preserve {
		p.emit("progress", "Проверка сохранённых данных", 12)
		if before, e = p.wdttPlusReadBefore(quiet, s); e != nil {
			return e
		}
		random := make([]byte, 16)
		_, _ = rand.Read(random)
		attempt = hex.EncodeToString(random)
		// From here a failure before verification restores the server from the backup.
		defer func() {
			if err == nil || verified {
				return
			}
			p.emit("progress", "Откат обновления", 95)
			restored, re := p.wdttPlusRecover(client, s, attempt)
			if re != nil {
				err = fmt.Errorf("%v. Автоматический откат не выполнен: %v", err, re)
			} else if restored {
				err = fmt.Errorf("%v. Сервер возвращён к состоянию до обновления", err)
			}
		}()
		p.emit("progress", "Страховочная копия сервера", 15)
		out, e := exec(asRoot(wdttPlusServerPaths.prepare(attempt)))
		if wdttPlusMarker(out, "WDTT_UPDATE_BACKUP") == "stale" {
			return fmt.Errorf("На сервере осталась незавершённая страховочная копия прошлого обновления (/var/tmp/wdtt-plus-update-backup). " +
				"Обновление заблокировано до её проверки или отката")
		}
		if e != nil || wdttPlusMarker(out, "WDTT_UPDATE_BACKUP") != "ready" {
			return fmt.Errorf("Не удалось подготовить страховочную копию обновления")
		}
		// The first read was preflight only: the stopped daemon has now flushed the database.
		if before, e = p.wdttPlusReadBefore(quiet, s); e != nil {
			return e
		}
		if ownership == wdttPlusPreservedData || ownership == wdttPlusUninstalledData {
			if out, e := exec(asRoot(wdttPlusMarkFor(ownership))); e != nil || wdttPlusMarker(out, "WDTT_PRESERVED_MARKER") != "ready" {
				return fmt.Errorf("Не удалось подготовить сохранённые данные WDTT Plus на сервере (нужны /etc/wdtt/passwords.json и wg-keys.dat)")
			}
		}
	}
	database, e := wdttPlusDatabase(before, s)
	if e != nil {
		return e
	}

	p.emit("progress", "Передача файлов на VPS", 30)
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
	files, e := sftp.NewClient(client)
	if e != nil {
		return e
	}
	for path, data := range map[string][]byte{staging + "/deploy.sh": script, staging + "/server": server, staging + "/passwords.json": database} {
		if e = uploadFile(files, path, data); e != nil {
			files.Close()
			return e
		}
	}
	files.Close()

	command := wdttPlusCommand(s, staging, preserve)
	if preserve {
		p.emit("progress", "Обновление с сохранением данных", 45)
		if ownership == wdttPlusAndroidDeploy || ownership == wdttPlusLegacyDeploy {
			command = wdttPlusClearStaleMarker + "\n" + command
		}
		command = wdttPlusServerPaths.guarded(command, attempt)
	} else {
		p.emit("progress", "Установка и запуск сервиса", 45)
		command = wdttPlusServerPaths.context() + "\nwdtt_lock\n" + command
	}
	output, e := exec(asRoot(command))
	if e != nil {
		return fmt.Errorf("Установщик завершился с ошибкой: %w", e)
	}
	if !wdttPlusSuccess.MatchString(output) {
		return fmt.Errorf("Установщик не подтвердил запуск сервиса")
	}
	p.emit("progress", "Проверка сервера", 80)
	verify, e := exec(asRoot(wdttPlusVerify))
	if e != nil {
		return e
	}
	if missing := wdttPlusVerifyFailures(verify, serverSHA); len(missing) > 0 {
		return fmt.Errorf("Установщик завершился, но проверка не прошла: %s", strings.Join(missing, ", "))
	}
	read, e := quiet(asRoot(wdttPlusReadDB))
	after, pe := wdttPlusParseRead(read)
	switch {
	case pe != nil:
		return pe
	case e != nil || after == nil:
		return fmt.Errorf("После установки не найдена база passwords.json")
	case before != nil:
		e = wdttPlusValidatePreserved(before, after, time.Now())
	default:
		e = wdttPlusValidateDB(after)
	}
	if e != nil {
		return e
	}
	verified = true
	if preserve {
		// The new server is verified: a failed cleanup is a warning, never a reason to roll back.
		if out, e := exec(asRoot(wdttPlusServerPaths.cleanup(attempt))); e != nil || wdttPlusMarker(out, "WDTT_UPDATE_CLEANUP") != "ok" {
			p.emit("log", "Сервер обновлён, но страховочная копия /var/tmp/wdtt-plus-update-backup не удалена.\n", nil)
		}
	}
	p.emit("config", "", wdttPlusLink(s))
	p.emit("progress", "Готово", 100)
	return nil
}

// wdttPlusReadBefore reads the database a preserving update keeps; it must be there and valid.
func (p *protocol) wdttPlusReadBefore(quiet func(string) (string, error), s map[string]string) (wdttPlusDB, error) {
	out, e := quiet(asRoot(wdttPlusReadDB))
	db, pe := wdttPlusParseRead(out)
	if pe != nil {
		return nil, pe
	}
	if e != nil {
		return nil, e
	}
	if db == nil {
		return nil, fmt.Errorf("Обновление с сохранением остановлено: на сервере нет passwords.json, а пустая база вместо неизвестного состояния не создаётся")
	}
	return db, wdttPlusValidateForPreserving(db, s["password"])
}

// wdttPlusRecover is recoverServerUpdateAfterFailure: on its own connection and context, so it
// also runs after the user stopped the deploy; it acts only on this attempt's backup.
func (p *protocol) wdttPlusRecover(client *ssh.Client, s map[string]string, attempt string) (restored bool, e error) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Minute)
	defer cancel()
	recovery := &protocol{output: json.NewEncoder(io.Discard), ctx: ctx}
	run := func(command string) (string, error) {
		return recovery.run(client, s, asRoot(command), 4*time.Minute, false)
	}
	owned, e := "", fmt.Errorf("stopped")
	if p.ctx.Err() == nil {
		owned, e = run(wdttPlusServerPaths.owned(attempt))
	}
	if e != nil {
		if client, e = recovery.dial(s, false); e != nil {
			return false, e
		}
		defer client.Close()
		if owned, e = run(wdttPlusServerPaths.owned(attempt)); e != nil {
			return false, e
		}
	}
	if wdttPlusMarker(owned, "WDTT_UPDATE_ATTEMPT_OWNED") != "1" {
		return false, nil
	}
	probe, _ := run(wdttPlusServerPaths.probe())
	switch wdttPlusBackupStatus(probe) {
	case "prepared_valid":
		out, e := run(wdttPlusServerPaths.rollback(attempt))
		if e != nil || wdttPlusMarker(out, "WDTT_ROLLBACK") != "ok" {
			return false, fmt.Errorf("сервер не подтвердил откат; страховочная копия оставлена в /var/tmp/wdtt-plus-update-backup")
		}
		return true, nil
	case "preparation_incomplete":
		out, e := run(wdttPlusServerPaths.cancel(attempt))
		if e != nil || wdttPlusMarker(out, "WDTT_UPDATE_PREPARATION") != "cancelled" {
			return false, fmt.Errorf("не удалось отменить незавершённую подготовку копии")
		}
		return false, nil
	case "committed_valid", "none":
		return false, nil
	}
	return false, fmt.Errorf("страховочная копия не прошла проверку (%s); автоматический откат запрещён",
		wdttPlusMarker(probe, "WDTT_UPDATE_BACKUP_DIAGNOSTIC"))
}

// wdttPlusUninstallSteps are performUninstall's steps for the preserving removal qThrone offers;
// each may fail on its own, the final check decides.
func wdttPlusUninstallSteps(s map[string]string) []struct{ title, command string } {
	dtls, wg, sshPort := s["peerPort"], s["wgPort"], s["sshPort"]
	firewall := `if command -v iptables >/dev/null 2>&1; then for i in 1 2 3 4 5; do ` +
		`for iface in $(ls /sys/class/net 2>/dev/null || true); do ` +
		`iptables -t nat -D POSTROUTING -s 10.66.66.0/24 -o "$iface" -m comment --comment WDTT_MANAGED -j MASQUERADE 2>/dev/null || true; done; ` +
		`iptables -t mangle -D FORWARD -s 10.66.66.0/24 -p tcp -m tcp --tcp-flags SYN,RST SYN -m comment --comment WDTT_MANAGED -j TCPMSS --clamp-mss-to-pmtu 2>/dev/null || true; ` +
		`iptables -t mangle -D FORWARD -d 10.66.66.0/24 -p tcp -m tcp --tcp-flags SYN,RST SYN -m comment --comment WDTT_MANAGED -j TCPMSS --clamp-mss-to-pmtu 2>/dev/null || true; `
	for _, rule := range []string{"udp --dport " + dtls, "udp --dport " + wg, "udp --dport 56000", "udp --dport 56001", "tcp --dport " + sshPort, "tcp --dport 22"} {
		firewall += `iptables -D INPUT -p ` + rule + ` -m comment --comment WDTT_MANAGED -j ACCEPT 2>/dev/null || true; `
	}
	firewall += `iptables -D FORWARD -i wdtt0 -m comment --comment WDTT_MANAGED -j ACCEPT 2>/dev/null || true; ` +
		`iptables -D FORWARD -o wdtt0 -m comment --comment WDTT_MANAGED -j ACCEPT 2>/dev/null || true; done; `
	for _, chain := range []string{"INPUT", "FORWARD", "-t nat POSTROUTING", "-t mangle FORWARD"} {
		table, name := "", chain
		if parts := strings.Fields(chain); len(parts) == 3 {
			table, name = parts[0]+" "+parts[1]+" ", parts[2]
		}
		firewall += `while rule_no=$(iptables ` + table + `-L ` + name + ` --line-numbers -n 2>/dev/null | awk '/WDTT_MANAGED/ {print $1; exit}'); ` +
			`[ -n "$rule_no" ]; do iptables ` + table + `-D ` + name + ` "$rule_no" 2>/dev/null || break; done; `
	}
	firewall += `fi; if command -v nft >/dev/null 2>&1; then nft delete table ip wdtt 2>/dev/null || true; ` +
		`nft delete table inet wdtt 2>/dev/null || true; nft delete table inet wdtt_mangle 2>/dev/null || true; fi`
	return []struct{ title, command string }{
		{"Остановка сервиса", "systemctl unmask wdtt 2>/dev/null || true; systemctl stop wdtt 2>/dev/null || true; " +
			"systemctl disable wdtt 2>/dev/null || true; rm -f /etc/systemd/system/wdtt.service; systemctl daemon-reload 2>/dev/null || true"},
		{"Удаление сервера", "pkill -x wdtt-server 2>/dev/null || true; rm -f /usr/local/bin/wdtt-server"},
		{"Очистка фаервола", firewall},
		// The original's checks here stand alone without set -e and do nothing; here they stop the step.
		{"Сохранение данных", "ip link show wdtt0 >/dev/null 2>&1 && ip link del wdtt0 2>/dev/null || true; " +
			"{ [ -d /etc/wdtt ] && [ ! -L /etc/wdtt ]; } || exit 1; chmod 700 /etc/wdtt; " +
			"if [ -f /etc/wdtt/passwords.json ]; then chmod 600 /etc/wdtt/passwords.json; fi; " +
			"if [ -f /etc/wdtt/wg-keys.dat ]; then chmod 600 /etc/wdtt/wg-keys.dat; fi"},
		{"Очистка системных настроек", "rm -f /etc/sysctl.d/99-wdtt.conf /tmp/deploy.sh /tmp/wdtt-server /tmp/wdtt-passwords.json.new; " +
			"sysctl --system >/dev/null 2>&1 || true"},
	}
}

const wdttPlusUninstallVerify = `printf 'WDTT_UNINSTALL_SERVICE=%s\n' "$([ ! -e /etc/systemd/system/wdtt.service ] && echo 0 || echo 1)"; ` +
	`printf 'WDTT_UNINSTALL_BINARY=%s\n' "$([ ! -e /usr/local/bin/wdtt-server ] && echo 0 || echo 1)"; ` +
	`printf 'WDTT_UNINSTALL_INTERFACE=%s\n' "$(ip link show wdtt0 >/dev/null 2>&1 && echo 1 || echo 0)"; ` +
	`printf 'WDTT_UNINSTALL_PROCESS=%s\n' "$(pgrep -x wdtt-server >/dev/null 2>&1 && echo 1 || echo 0)"; ` +
	`printf 'WDTT_UNINSTALL_SYSCTL=%s\n' "$([ ! -e /etc/sysctl.d/99-wdtt.conf ] && echo 0 || echo 1)"; ` +
	`printf 'WDTT_UNINSTALL_FIREWALL=%s\n' "$({ { ! command -v iptables-save >/dev/null 2>&1 || ! iptables-save 2>/dev/null | grep -q WDTT_MANAGED; } && ` +
	`{ ! command -v nft >/dev/null 2>&1 || ! nft list tables 2>/dev/null | grep -Eq '^table (ip|inet) wdtt(_mangle)?$'; }; } && echo 0 || echo 1)"; ` +
	`printf 'WDTT_UNINSTALL_DATA=%s\n' "$([ -f /etc/wdtt/passwords.json ] && [ -f /etc/wdtt/wg-keys.dat ] && [ -f /etc/wdtt/.android-deploy-preserved ] && echo preserved || echo invalid)"`

// wdttPlusUninstallLeft lists what the removal left behind; the data must stay, marked.
func wdttPlusUninstallLeft(output string) []string {
	var left []string
	for _, c := range []struct{ name, want, what string }{{"WDTT_UNINSTALL_SERVICE", "0", "служба"}, {"WDTT_UNINSTALL_BINARY", "0", "сервер"},
		{"WDTT_UNINSTALL_INTERFACE", "0", "интерфейс wdtt0"}, {"WDTT_UNINSTALL_PROCESS", "0", "процесс wdtt-server"},
		{"WDTT_UNINSTALL_SYSCTL", "0", "настройки sysctl"}, {"WDTT_UNINSTALL_FIREWALL", "0", "правила фаервола"},
		{"WDTT_UNINSTALL_DATA", "preserved", "сохранённые данные с меткой"}} {
		if wdttPlusMarker(output, c.name) != c.want {
			left = append(left, c.what)
		}
	}
	return left
}

// uninstallWdttPlus removes the service and keeps /etc/wdtt marked, so a later deploy updates it.
func (p *protocol) uninstallWdttPlus(s map[string]string, ownership wdttPlusOwnership, exec func(string) (string, error)) error {
	if e := wdttPlusMayUninstall(ownership); e != nil {
		return e
	}
	p.emit("progress", "Защита сохраняемых данных", 15)
	if out, e := exec(asRoot(wdttPlusMarkFor(ownership))); e != nil || wdttPlusMarker(out, "WDTT_PRESERVED_MARKER") != "ready" {
		if p.ctx.Err() != nil {
			return p.ctx.Err()
		}
		return fmt.Errorf("Не удалось отметить сохраняемые данные WDTT Plus (нужны /etc/wdtt/passwords.json и wg-keys.dat). Удаление остановлено")
	}
	for i, step := range wdttPlusUninstallSteps(s) {
		p.emit("progress", step.title, 25+i*14)
		if _, e := exec(asRoot(step.command)); p.ctx.Err() != nil {
			return p.ctx.Err()
		} else if e != nil {
			p.emit("log", step.title+": шаг завершился с ошибкой, итог покажет проверка.\n", nil)
		}
	}
	p.emit("progress", "Проверка результата", 95)
	out, e := exec(asRoot(wdttPlusUninstallVerify))
	if e != nil {
		return e
	}
	if left := wdttPlusUninstallLeft(out); len(left) > 0 {
		return fmt.Errorf("Удаление завершилось не полностью (%s). Проверьте сервер и повторите удаление", strings.Join(left, ", "))
	}
	p.emit("config", "", "")
	p.emit("progress", "Готово", 100)
	return nil
}

// wdttPlusCommand runs the staged deploy.sh with the app's environment; passwords.json is
// always staged, as performDeploy does without an import.
func wdttPlusCommand(s map[string]string, staging string, preserve bool) string {
	mode, keep := "fresh", "0"
	if preserve {
		mode, keep = "preserve", "1"
	}
	command := "env WDTT_DTLS_PORT=" + s["peerPort"] + " WDTT_WG_PORT=" + s["wgPort"] + " WDTT_SSH_PORT=" + s["sshPort"] +
		" WDTT_INSTALL_MODE=" + mode + " WDTT_PRESERVE_DATA=" + keep
	if s["dns"] != "" {
		command += " WDTT_DNS=" + quote(s["dns"])
	}
	return command + " WDTT_STAGED_SERVER=" + quote(staging+"/server") + " WDTT_STAGED_DB=" + quote(staging+"/passwords.json") +
		" bash " + quote(staging+"/deploy.sh")
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
