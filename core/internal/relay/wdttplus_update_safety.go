package relay

import "strings"

// Server-side scripts of WDTT Plus's update safety (ServerUpdateSafety.kt, f706ccd): before a
// preserving update the stopped service's config, binary and unit are copied to a private,
// checksummed backup; a failed update restores it, a verified one discards it.

// wdttPlusUpdatePaths are injectable only for tests on a temporary root.
type wdttPlusUpdatePaths struct{ backup, config, binary, unit, lock string }

var wdttPlusServerPaths = wdttPlusUpdatePaths{
	backup: "/var/tmp/wdtt-plus-update-backup",
	config: "/etc/wdtt",
	binary: "/usr/local/bin/wdtt-server",
	unit:   "/etc/systemd/system/wdtt.service",
	lock:   "/run/lock/wdtt-plus-update.lock",
}

func (paths wdttPlusUpdatePaths) context() string {
	return `set -euo pipefail
umask 077
BACKUP=` + quote(paths.backup) + `
CONFIG=` + quote(paths.config) + `
BINARY=` + quote(paths.binary) + `
UNIT=` + quote(paths.unit) + `
LOCK=` + quote(paths.lock) + `
wdtt_lock() {
  command -v flock >/dev/null || { echo 'error: для безопасного обновления требуется flock'; exit 2; }
  [ ! -L "$LOCK" ] || { echo 'error: небезопасный файл блокировки обновления'; exit 2; }
  if [ ! -e "$LOCK" ]; then
    # Atomic exclusive creation; never truncate an existing path in a
    # shared lock directory, including one created between these checks.
    (set -C; : > "$LOCK") 2>/dev/null || true
  fi
  wdtt_regular "$LOCK" && [ "$(stat -c %u "$LOCK")" = "$(id -u)" ] &&
    [ "$(stat -c %h "$LOCK")" = 1 ] || exit 2
  exec 9<"$LOCK"
  flock -n 9 || { echo 'error: другая операция обновления ещё выполняется'; exit 2; }
}
wdtt_update_phase() {
  printf '%s\n' "$1" > "$BACKUP/state.next"
  mv -f -- "$BACKUP/state.next" "$BACKUP/state"
}
wdtt_regular() { [ -f "$1" ] && [ ! -L "$1" ]; }
wdtt_private_backup() {
  [ -d "$BACKUP" ] && [ ! -L "$BACKUP" ] &&
    [ "$(stat -c %u "$BACKUP")" = "$(id -u)" ] &&
    [ "$(stat -c %a "$BACKUP")" = 700 ]
}
wdtt_safe_tree() {
  [ -d "$1" ] && [ ! -L "$1" ] || return 1
  local unsafe
  unsafe=$(find "$1" -mindepth 1 \( -type l -o ! \( -type f -o -type d \) \) -print -quit) || return 1
  [ -z "$unsafe" ]
}
wdtt_manifest() {
  (cd "$BACKUP"; find . -mindepth 1 -type f \
    ! -path ./state ! -path ./state.next ! -path ./checksums \
    ! -path ./failure -print0 | LC_ALL=C sort -z | xargs -0 -r sha256sum --)
}
wdtt_quarantine() {
  local destination
  destination=$(mktemp -d "${BACKUP}.incomplete.XXXXXX")
  mv -T -- "$BACKUP" "$destination/copy"
  printf 'WDTT_UPDATE_ARCHIVE=%s\n' "$destination"
}
wdtt_discard_backup() {
  local destination
  destination=$(mktemp -d "${BACKUP}.completed.XXXXXX")
  mv -T -- "$BACKUP" "$destination/copy"
  rm -rf -- "$destination"
}
wdtt_stop_service() {
  if ! systemctl stop wdtt; then
    [ "$(systemctl show -p LoadState --value wdtt)" = not-found ] &&
      ! systemctl is-active --quiet wdtt && command -v pgrep >/dev/null &&
      ! pgrep -x wdtt-server >/dev/null || return 1
  fi
  ! systemctl is-active --quiet wdtt
}`
}

func wdttPlusAttemptGuard(attempt string) string {
	return `wdtt_regular "$BACKUP/attempt" && [ "$(cat "$BACKUP/attempt")" = ` + quote(attempt) + ` ] || exit 2`
}

// prepare copies the stopped service: a stopped daemon cannot rewrite passwords.json between
// copy and comparison. A failed preparation restarts the service and moves the copy aside.
func (paths wdttPlusUpdatePaths) prepare(attempt string) string {
	return paths.context() + `
wdtt_lock
[ ! -e "$BACKUP" ] && [ ! -L "$BACKUP" ] || { echo WDTT_UPDATE_BACKUP=stale; exit 3; }
stage=preflight
for tool in cp cmp diff sha256sum find sort xargs stat mktemp du df awk; do
  command -v "$tool" >/dev/null || { printf 'error: для безопасного обновления требуется %s\n' "$tool"; exit 4; }
done
[ ! -e "$CONFIG" ] || wdtt_safe_tree "$CONFIG" || { echo 'error: небезопасная конфигурация'; exit 4; }
[ ! -L "$CONFIG" ] || exit 4
for path in "$BINARY" "$UNIT"; do
  [ ! -e "$path" ] && [ ! -L "$path" ] || wdtt_regular "$path" || exit 4
done
# Refuse an obviously impossible snapshot before interrupting a healthy service.
required_kib=32768
for path in "$CONFIG" "$BINARY" "$UNIT"; do
  if [ -e "$path" ]; then
    size_kib=$(du -sk -- "$path" | awk '{print $1}')
    [[ "$size_kib" =~ ^[0-9]+$ ]] || exit 4
    required_kib=$((required_kib + size_kib))
  fi
done
available_kib=$(df -Pk -- "$(dirname "$BACKUP")" | awk 'NR==2 {print $4}')
[[ "$available_kib" =~ ^[0-9]+$ ]] && [ "$available_kib" -ge "$required_kib" ] || {
  echo 'error: недостаточно свободного места для полной страховочной копии'; exit 4;
}
install -d -m 700 "$BACKUP"
ready=0
wdtt_prepare_exit() {
  local status=$?
  trap - EXIT HUP INT TERM
  if [ "$ready" = 0 ]; then
    set +e
    printf '%s\n' "$stage" > "$BACKUP/failure"
    wdtt_update_phase preparation_failed
    restored=1
    if [ -f "$BACKUP/was_active" ]; then
      systemctl start wdtt && systemctl is-active --quiet wdtt || restored=0
    fi
    if [ "$restored" = 1 ]; then wdtt_quarantine; fi
    printf 'error: подготовка страховочной копии остановлена на этапе %s (код %s)\n' "$stage" "$status"
    [ "$status" != 0 ] || status=1
  fi
  exit "$status"
}
trap wdtt_prepare_exit EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
stage=journal
printf '2\n' > "$BACKUP/format"
printf '%s\n' ` + quote(attempt) + ` > "$BACKUP/attempt"
wdtt_update_phase preparing
stage=service_state
if systemctl is-active --quiet wdtt; then
  wdtt_regular "$UNIT" || exit 4
  touch "$BACKUP/was_active"
fi
if systemctl is-enabled --quiet wdtt; then touch "$BACKUP/was_enabled"; fi
stage=stop_service
if [ -f "$BACKUP/was_active" ]; then
  printf 'WDTT_PROGRESS|0.057|Создаю согласованную копию. Служба WDTT приостановлена на время обновления…\n'
fi
# Also stop an inactive/starting unit: it must not activate during copying.
wdtt_stop_service || exit 5
stage=copy_config
if [ -d "$CONFIG" ]; then
  cp -a -- "$CONFIG" "$BACKUP/config"
  wdtt_safe_tree "$BACKUP/config" || exit 5
  stage=verify_config
  # Compare in both directions: missing or extra files/directories are also a failure.
  diff -qr -- "$CONFIG" "$BACKUP/config" >/dev/null || exit 5
  touch "$BACKUP/had_config"
fi
stage=copy_binary
if wdtt_regular "$BINARY"; then
  cp -a -- "$BINARY" "$BACKUP/wdtt-server"
  cmp -s -- "$BINARY" "$BACKUP/wdtt-server" || exit 5
  touch "$BACKUP/had_binary"
fi
stage=copy_unit
if wdtt_regular "$UNIT"; then
  cp -a -- "$UNIT" "$BACKUP/wdtt.service"
  cmp -s -- "$UNIT" "$BACKUP/wdtt.service" || exit 5
  touch "$BACKUP/had_service"
fi
stage=manifest
wdtt_manifest > "$BACKUP/checksums"
stage=publish
wdtt_update_phase prepared
ready=1
echo WDTT_UPDATE_BACKUP=ready`
}

// probe reports WDTT_UPDATE_BACKUP_STATUS: none, preparation_incomplete, prepared_valid,
// committed_valid, prepared_corrupted or unknown_state.
func (paths wdttPlusUpdatePaths) probe() string {
	return paths.context() + `
wdtt_backup_result() {
  printf 'WDTT_UPDATE_BACKUP_STATUS=%s\n' "$1"
  printf 'WDTT_UPDATE_BACKUP_DIAGNOSTIC=%s\n' "$2"
  exit 0
}
if [ ! -e "$BACKUP" ] && [ ! -L "$BACKUP" ]; then
  wdtt_backup_result none absent
fi
if [ -L "$BACKUP" ] || [ ! -d "$BACKUP" ]; then
  wdtt_backup_result prepared_corrupted unsafe_backup_path
fi
wdtt_private_backup || wdtt_backup_result prepared_corrupted unsafe_backup_permissions
if [ ! -f "$BACKUP/state" ] || [ -L "$BACKUP/state" ]; then
  wdtt_backup_result prepared_corrupted missing_or_unsafe_state
fi
backup_state=$(cat "$BACKUP/state" 2>/dev/null || true)
if [ "$backup_state" = preparing ] || [ "$backup_state" = preparation_failed ]; then
  wdtt_regular "$BACKUP/format" && [ "$(cat "$BACKUP/format")" = 2 ] ||
    wdtt_backup_result unknown_state unknown_preparation
  wdtt_safe_tree "$BACKUP" || wdtt_backup_result prepared_corrupted unsafe_preparation
  unknown=$(find "$BACKUP" -mindepth 1 -maxdepth 1 \
    ! -name state ! -name state.next ! -name format ! -name attempt ! -name checksums ! -name failure \
    ! -name config ! -name wdtt-server ! -name wdtt.service \
    ! -name had_config ! -name had_binary ! -name had_service \
    ! -name was_active ! -name was_enabled -print -quit)
  [ -z "$unknown" ] || wdtt_backup_result prepared_corrupted unknown_backup_entry
  wdtt_backup_result preparation_incomplete preparation_not_finished
fi
if [ "$backup_state" != prepared ] && [ "$backup_state" != applying ] &&
   [ "$backup_state" != rolling_back ] && [ "$backup_state" != committed ]; then
  wdtt_backup_result unknown_state state_not_prepared
fi
if [ "$backup_state" != prepared ]; then
  wdtt_regular "$BACKUP/format" && [ "$(cat "$BACKUP/format")" = 2 ] ||
    wdtt_backup_result unknown_state legacy_state_not_prepared
fi
for marker in had_config had_binary had_service was_active was_enabled; do
  marker_path="$BACKUP/$marker"
  if [ -e "$marker_path" ] || [ -L "$marker_path" ]; then
    [ -f "$marker_path" ] && [ ! -L "$marker_path" ] ||
      wdtt_backup_result prepared_corrupted unsafe_marker
  fi
done
if [ -f "$BACKUP/had_config" ]; then
  [ -d "$BACKUP/config" ] && [ ! -L "$BACKUP/config" ] ||
    wdtt_backup_result prepared_corrupted missing_config
  unsafe=$(find "$BACKUP/config" -mindepth 1 \( -type l -o ! \( -type f -o -type d \) \) -print -quit) ||
    wdtt_backup_result prepared_corrupted unreadable_config_tree
  [ -z "$unsafe" ] || wdtt_backup_result prepared_corrupted unsafe_config_tree
elif [ -e "$BACKUP/config" ] || [ -L "$BACKUP/config" ]; then
  wdtt_backup_result prepared_corrupted unexpected_config
fi
if [ -f "$BACKUP/had_binary" ]; then
  [ -f "$BACKUP/wdtt-server" ] && [ ! -L "$BACKUP/wdtt-server" ] ||
    wdtt_backup_result prepared_corrupted missing_binary
elif [ -e "$BACKUP/wdtt-server" ] || [ -L "$BACKUP/wdtt-server" ]; then
  wdtt_backup_result prepared_corrupted unexpected_binary
fi
if [ -f "$BACKUP/had_service" ]; then
  [ -f "$BACKUP/wdtt.service" ] && [ ! -L "$BACKUP/wdtt.service" ] ||
    wdtt_backup_result prepared_corrupted missing_service
elif [ -e "$BACKUP/wdtt.service" ] || [ -L "$BACKUP/wdtt.service" ]; then
  wdtt_backup_result prepared_corrupted unexpected_service
fi
if { [ -f "$BACKUP/was_active" ] || [ -f "$BACKUP/was_enabled" ]; } &&
   [ ! -f "$BACKUP/had_service" ]; then
  wdtt_backup_result prepared_corrupted service_state_without_service
fi
unknown=$(find "$BACKUP" -mindepth 1 -maxdepth 1 \
  ! -name state ! -name had_config ! -name config \
  ! -name had_binary ! -name wdtt-server \
  ! -name had_service ! -name wdtt.service \
  ! -name was_active ! -name was_enabled ! -name format ! -name attempt ! -name checksums \
  ! -name failure -print -quit)
[ -z "$unknown" ] || wdtt_backup_result prepared_corrupted unknown_backup_entry
if [ -e "$BACKUP/format" ] || [ -L "$BACKUP/format" ]; then
  wdtt_regular "$BACKUP/format" && [ "$(cat "$BACKUP/format")" = 2 ] ||
    wdtt_backup_result prepared_corrupted invalid_format
  wdtt_regular "$BACKUP/checksums" || wdtt_backup_result prepared_corrupted missing_checksums
  expected=$(cat "$BACKUP/checksums") || wdtt_backup_result prepared_corrupted unreadable_checksums
  actual=$(wdtt_manifest) || wdtt_backup_result prepared_corrupted unreadable_backup
  [ "$expected" = "$actual" ] || wdtt_backup_result prepared_corrupted checksum_mismatch
elif [ -e "$BACKUP/checksums" ] || [ -L "$BACKUP/checksums" ] ||
     [ -e "$BACKUP/attempt" ] || [ -L "$BACKUP/attempt" ]; then
  wdtt_backup_result prepared_corrupted missing_format
fi
if [ "$backup_state" = committed ]; then wdtt_backup_result committed_valid committed; fi
printf 'WDTT_UPDATE_BACKUP_STATUS=prepared_valid\n'
printf 'WDTT_UPDATE_BACKUP_DIAGNOSTIC=prepared\n'`
}

func (paths wdttPlusUpdatePaths) probeIn() string {
	return `probe=$( (` + paths.probe() + `
) )`
}

// guarded runs an update step only while this attempt's validated backup is in place.
func (paths wdttPlusUpdatePaths) guarded(command, attempt string) string {
	return paths.context() + `
wdtt_lock
wdtt_regular "$BACKUP/state" || exit 2
case "$(cat "$BACKUP/state")" in prepared|applying) ;; *) exit 2 ;; esac
` + wdttPlusAttemptGuard(attempt) + `
` + paths.probeIn() + `
printf '%s\n' "$probe" | grep -qx 'WDTT_UPDATE_BACKUP_STATUS=prepared_valid' || exit 2
wdtt_update_phase applying
` + command
}

// owned tells whether the backup on the server belongs to this attempt, so a stale attempt
// never rolls back another device's update.
func (paths wdttPlusUpdatePaths) owned(attempt string) string {
	return paths.context() + `
if wdtt_regular "$BACKUP/attempt" && [ "$(cat "$BACKUP/attempt")" = ` + quote(attempt) + ` ]; then
  echo WDTT_UPDATE_ATTEMPT_OWNED=1
fi`
}

// cancel ends an interrupted preparation, which has not changed the installation.
func (paths wdttPlusUpdatePaths) cancel(attempt string) string {
	return paths.context() + `
wdtt_lock
` + wdttPlusAttemptGuard(attempt) + `
wdtt_private_backup || exit 2
wdtt_safe_tree "$BACKUP" || exit 2
wdtt_regular "$BACKUP/format" && [ "$(cat "$BACKUP/format")" = 2 ] || exit 2
wdtt_regular "$BACKUP/state" || exit 2
case "$(cat "$BACKUP/state")" in preparing|preparation_failed) ;; *) exit 2 ;; esac
wdtt_regular "$BINARY" && [ -x "$BINARY" ] && wdtt_regular "$UNIT" || exit 3
wdtt_regular "$CONFIG/passwords.json" && [ -s "$CONFIG/passwords.json" ] || exit 3
if [ -f "$BACKUP/was_active" ]; then
  systemctl start wdtt
  systemctl is-active --quiet wdtt || exit 3
fi
wdtt_quarantine
echo WDTT_UPDATE_PREPARATION=cancelled`
}

// cleanup discards the backup of a verified update; the running service is checked first.
func (paths wdttPlusUpdatePaths) cleanup(attempt string) string {
	return paths.context() + `
wdtt_lock
` + wdttPlusAttemptGuard(attempt) + `
` + paths.probeIn() + `
printf '%s\n' "$probe" | grep -Eq '^WDTT_UPDATE_BACKUP_STATUS=(prepared_valid|committed_valid)$' || exit 2
wdtt_regular "$BINARY" && [ -x "$BINARY" ] && wdtt_regular "$UNIT" || exit 3
wdtt_regular "$CONFIG/passwords.json" && [ -s "$CONFIG/passwords.json" ] || exit 3
systemctl is-active --quiet wdtt || exit 3
if [ -f "$BACKUP/format" ]; then wdtt_update_phase committed; fi
# Move atomically out of the recovery path first. Interruption during deletion
# cannot turn a successfully committed update into an incomplete active backup.
wdtt_discard_backup
echo WDTT_UPDATE_CLEANUP=ok`
}

// rollback restores the backed-up binary, unit and config and the service's previous state.
func (paths wdttPlusUpdatePaths) rollback(attempt string) string {
	return paths.context() + `
wdtt_lock
` + wdttPlusAttemptGuard(attempt) + `
` + paths.probeIn() + `
printf '%s\n' "$probe" | grep -qx 'WDTT_UPDATE_BACKUP_STATUS=prepared_valid' || exit 2
[ ! -L "$CONFIG" ] && [ ! -L "$BINARY" ] && [ ! -L "$UNIT" ] || exit 3
wdtt_safe_tree "$BACKUP" || exit 3
if [ -f "$BACKUP/format" ]; then wdtt_update_phase rolling_back; fi
wdtt_stop_service || exit 3
if [ -f "$BACKUP/had_binary" ]; then
  install -m 755 "$BACKUP/wdtt-server" "$BINARY"
  cmp -s "$BACKUP/wdtt-server" "$BINARY" || exit 3
else rm -f -- "$BINARY"; fi
if [ -f "$BACKUP/had_service" ]; then
  install -m 644 "$BACKUP/wdtt.service" "$UNIT"
  cmp -s "$BACKUP/wdtt.service" "$UNIT" || exit 3
else rm -f -- "$UNIT"; fi
# The complete validated backup remains untouched until restoration and
# service verification succeed, including after an interrupted copy.
rm -rf -- "$CONFIG"
if [ -f "$BACKUP/had_config" ]; then
  cp -a -- "$BACKUP/config" "$CONFIG"
  diff -qr -- "$BACKUP/config" "$CONFIG" >/dev/null || exit 3
fi
systemctl daemon-reload
if [ -f "$BACKUP/was_enabled" ]; then
  systemctl enable wdtt >/dev/null 2>&1
elif [ -f "$BACKUP/had_service" ]; then
  systemctl disable wdtt >/dev/null 2>&1
fi
if [ -f "$BACKUP/was_active" ]; then
  systemctl restart wdtt
  sleep 2
  systemctl is-active --quiet wdtt || exit 3
else
  ! systemctl is-active --quiet wdtt || exit 3
fi
for pair in 'had_binary' 'had_service' 'had_config'; do
  case "$pair" in had_binary) path="$BINARY" ;; had_service) path="$UNIT" ;; had_config) path="$CONFIG" ;; esac
  if [ -f "$BACKUP/$pair" ]; then
    [ -e "$path" ] && [ ! -L "$path" ] || exit 3
  else [ ! -e "$path" ] && [ ! -L "$path" ] || exit 3; fi
done
wdtt_discard_backup
echo WDTT_ROLLBACK=ok`
}

// wdttPlusBackupStatus is the WDTT_UPDATE_BACKUP_STATUS a probe printed, "" when it printed none.
func wdttPlusBackupStatus(output string) string { return wdttPlusMarker(output, "WDTT_UPDATE_BACKUP_STATUS") }

// wdttPlusMarker is the value of the first NAME=value line, as markerValue in WDTT Plus.
func wdttPlusMarker(output, name string) string {
	for _, line := range strings.Split(output, "\n") {
		if value, ok := strings.CutPrefix(line, name+"="); ok {
			return strings.TrimSpace(value)
		}
	}
	return ""
}
