package relay

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"os"
	osexec "os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"
)

// Server scripts that need GNU coreutils and POSIX permissions run only on Linux (as on a VPS).
func wdttPlusLinux(t *testing.T) string {
	t.Helper()
	if runtime.GOOS != "linux" {
		t.Skip("server scripts need GNU coreutils and POSIX permissions")
	}
	bash, err := osexec.LookPath("bash")
	if err != nil {
		t.Skip("bash is not available")
	}
	return bash
}

func wdttPlusShell(t *testing.T, script string, env ...string) (string, error) {
	t.Helper()
	command := osexec.Command(wdttPlusLinux(t), "-c", script)
	command.Env = append(os.Environ(), env...)
	out, err := command.CombinedOutput()
	return string(out), err
}

func wdttPlusWrite(t *testing.T, root, name, content string, mode os.FileMode) {
	t.Helper()
	path := filepath.Join(root, name)
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(content), mode); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(path, mode); err != nil {
		t.Fatal(err)
	}
}

const wdttPlusTestUnit = "# Managed by WDTT Plus Android deploy\n# WDTT deploy compatibility: 1\n[Service]\n" +
	"ExecStartPre=-/usr/bin/env bash -c \"ip link del wdtt0\"\n" +
	"ExecStart=/usr/local/bin/wdtt-server -listen 0.0.0.0:56000 -wg-port 56001 -config-dir /etc/wdtt -wg-backend auto\n"

const wdttPlusTestDB = `{"main_password":"old-password","passwords":{},"devices":{}}`

func wdttPlusKeptData(t *testing.T, root string) {
	t.Helper()
	wdttPlusWrite(t, root, "etc/wdtt/passwords.json", wdttPlusTestDB, 0o600)
	wdttPlusWrite(t, root, "etc/wdtt/wg-keys.dat", "keys\n", 0o600)
	if err := os.Chmod(filepath.Join(root, "etc/wdtt"), 0o700); err != nil {
		t.Fatal(err)
	}
}

func wdttPlusInstalled(t *testing.T, root, unit string) {
	t.Helper()
	wdttPlusKeptData(t, root)
	wdttPlusWrite(t, root, "etc/systemd/system/wdtt.service", unit, 0o644)
	wdttPlusWrite(t, root, "usr/local/bin/wdtt-server", "#!/bin/sh\necho 20\n", 0o755)
}

// wdttPlusDeployMode runs deploy.sh's own validate_deploy_data_mode against the temporary root.
func wdttPlusDeployMode(t *testing.T, root string, preserve bool) (string, error) {
	t.Helper()
	wdttPlusWrite(t, root, "staged.json", wdttPlusTestDB, 0o600)
	mode, keep := "fresh", "0"
	if preserve {
		mode, keep = "preserve", "1"
	}
	source, err := filepath.Abs("testdata/wdttplus_deploy_mode.sh")
	if err != nil {
		t.Fatal(err)
	}
	return wdttPlusShell(t, `set -euo pipefail
WDTT_SERVER_BINARY_PATH="$ROOT/usr/local/bin/wdtt-server"
WDTT_SYSTEMD_UNIT_PATH="$ROOT/etc/systemd/system/wdtt.service"
WDTT_ANDROID_DEPLOY_MARKER="Managed by WDTT Plus Android deploy"
WDTT_ANDROID_PRESERVED_MARKER="Preserved by WDTT Plus Android deploy"
WDTT_STANDALONE_MARKER="Managed by WDTT Plus standalone server installer"
WDTT_STANDALONE_OWNERSHIP_PATH="$ROOT/var/lib/wdtt-server-installer/ownership"
WDTT_CONFIG_DIR="$ROOT/etc/wdtt"
WDTT_ACCESS_DB=passwords.json
WDTT_ACCESS_DB_PREVIOUS=passwords.json.previous
WDTT_WG_KEYS=wg-keys.dat
WDTT_STAGED_DB="$ROOT/staged.json"
. "$SOURCE"
validate_deploy_data_mode
echo MODE_OK`, "ROOT="+root, "SOURCE="+source, "WDTT_INSTALL_MODE="+mode, "WDTT_PRESERVE_DATA="+keep)
}

// Every server state is classified as WDTT Plus classifies it, and whatever qThrone then runs
// passes deploy.sh's own check of the install mode.
func TestWdttPlusScenariosPassDeployScript(t *testing.T) {
	wdttPlusLinux(t)
	legacyUnit := strings.Join(strings.Split(wdttPlusTestUnit, "\n")[2:], "\n")
	for _, c := range []struct {
		name  string
		setup func(t *testing.T, root string)
		want  wdttPlusOwnership
	}{
		{"clean server", func(*testing.T, string) {}, wdttPlusNoInstall},
		{"installed", func(t *testing.T, root string) { wdttPlusInstalled(t, root, wdttPlusTestUnit) }, wdttPlusAndroidDeploy},
		{"installed after a failed start", func(t *testing.T, root string) {
			wdttPlusInstalled(t, root, wdttPlusTestUnit)
			wdttPlusWrite(t, root, "etc/wdtt/.android-deploy-preserved", "Preserved by WDTT Plus Android deploy\n", 0o600)
		}, wdttPlusAndroidDeploy},
		{"old install without the marker", func(t *testing.T, root string) { wdttPlusInstalled(t, root, legacyUnit) }, wdttPlusLegacyDeploy},
		{"kept after qThrone removal", func(t *testing.T, root string) {
			wdttPlusKeptData(t, root)
			wdttPlusWrite(t, root, "etc/wdtt/.android-deploy-preserved", "Preserved by WDTT Plus Android deploy\n", 0o600)
		}, wdttPlusPreservedData},
		{"kept after deploy.sh uninstall", wdttPlusKeptData, wdttPlusPreservedData},
		// What qThrone 1.5 and 1.5.1 left: deploy.sh uninstall keeps the server's log and other files.
		{"kept after qThrone 1.5.1 removal", func(t *testing.T, root string) {
			wdttPlusKeptData(t, root)
			wdttPlusWrite(t, root, "etc/wdtt/server.log", "log\n", 0o644)
			wdttPlusWrite(t, root, "etc/wdtt/passwords.json.previous", wdttPlusTestDB, 0o644)
			_ = os.Chmod(filepath.Join(root, "etc/wdtt"), 0o755)
			_ = os.Chmod(filepath.Join(root, "etc/wdtt/wg-keys.dat"), 0o644)
		}, wdttPlusUninstalledData},
	} {
		t.Run(c.name, func(t *testing.T) {
			root := t.TempDir()
			c.setup(t, root)
			out, _ := wdttPlusShell(t, wdttPlusOwnershipProbe(root))
			ownership, err := wdttPlusOwnershipFrom(out)
			if err != nil || ownership != c.want {
				t.Fatalf("ownership %v %v, want %v\n%s", ownership, err, c.want, out)
			}
			preserve, err := wdttPlusMayInstall(ownership)
			if err != nil {
				t.Fatal(err)
			}
			if preserve {
				prepare := wdttPlusClearStaleMarker + " && echo WDTT_PRESERVED_MARKER=cleared"
				if ownership == wdttPlusPreservedData || ownership == wdttPlusUninstalledData {
					prepare = wdttPlusMarkFor(ownership)
				}
				if got := wdttPlusRoot(t, root, prepare); !strings.Contains(got, "WDTT_PRESERVED_MARKER=") {
					t.Fatalf("preparation failed: %s", got)
				}
				if ownership == wdttPlusUninstalledData {
					// Adopted data is then exactly what WDTT Plus itself keeps after a removal.
					out, _ := wdttPlusShell(t, wdttPlusOwnershipProbe(root))
					if again, _ := wdttPlusOwnershipFrom(out); again != wdttPlusPreservedData {
						t.Fatalf("adopted data is %v\n%s", again, out)
					}
				}
			}
			if out, err := wdttPlusDeployMode(t, root, preserve); err != nil || !strings.Contains(out, "MODE_OK") {
				t.Fatalf("deploy.sh refused: %v\n%s", err, out)
			}
		})
	}
}

// Regression: the original's update stops on the marker a failed start leaves next to the unit.
func TestWdttPlusStaleMarkerStopsDeployScript(t *testing.T) {
	wdttPlusLinux(t)
	root := t.TempDir()
	wdttPlusInstalled(t, root, wdttPlusTestUnit)
	wdttPlusWrite(t, root, "etc/wdtt/.android-deploy-preserved", "Preserved by WDTT Plus Android deploy\n", 0o600)
	if out, err := wdttPlusDeployMode(t, root, true); err == nil || !strings.Contains(out, "изменились после проверки") {
		t.Fatalf("deploy.sh accepted the stale marker: %s", out)
	}
}

func TestWdttPlusRefusedOwners(t *testing.T) {
	wdttPlusLinux(t)
	for _, c := range []struct {
		name      string
		setup     func(t *testing.T, root string)
		want      wdttPlusOwnership
		uninstall bool
	}{
		{"standalone installer", func(t *testing.T, root string) {
			wdttPlusInstalled(t, root, "# Managed by WDTT Plus standalone server installer\n"+wdttPlusTestUnit)
			wdttPlusWrite(t, root, "var/lib/wdtt-server-installer/ownership", "Managed by WDTT Plus standalone server installer\n", 0o600)
		}, wdttPlusStandalone, false},
		{"service without server", func(t *testing.T, root string) {
			wdttPlusKeptData(t, root)
			wdttPlusWrite(t, root, "etc/systemd/system/wdtt.service", wdttPlusTestUnit, 0o644)
		}, wdttPlusIncomplete, false},
		{"database without keys", func(t *testing.T, root string) {
			wdttPlusWrite(t, root, "etc/wdtt/passwords.json", wdttPlusTestDB, 0o600)
			_ = os.Chmod(filepath.Join(root, "etc/wdtt"), 0o700)
		}, wdttPlusIncomplete, false},
		{"kept data with a link inside", func(t *testing.T, root string) {
			wdttPlusKeptData(t, root)
			if err := os.Symlink("/etc/passwd", filepath.Join(root, "etc/wdtt/server.log")); err != nil {
				t.Fatal(err)
			}
		}, wdttPlusUnknown, false},
		{"foreign files without keys", func(t *testing.T, root string) {
			wdttPlusWrite(t, root, "etc/wdtt/passwords.json", wdttPlusTestDB, 0o600)
			wdttPlusWrite(t, root, "etc/wdtt/other.conf", "x", 0o600)
		}, wdttPlusUnknown, false},
		{"clean server", func(*testing.T, string) {}, wdttPlusNoInstall, true},
	} {
		t.Run(c.name, func(t *testing.T) {
			root := t.TempDir()
			c.setup(t, root)
			out, _ := wdttPlusShell(t, wdttPlusOwnershipProbe(root))
			ownership, err := wdttPlusOwnershipFrom(out)
			if err != nil || ownership != c.want {
				t.Fatalf("ownership %v %v, want %v\n%s", ownership, err, c.want, out)
			}
			if c.uninstall {
				if wdttPlusMayUninstall(ownership) == nil {
					t.Fatal("removal allowed")
				}
			} else if _, err := wdttPlusMayInstall(ownership); err == nil {
				t.Fatal("deploy allowed")
			}
		})
	}
	if _, err := wdttPlusOwnershipFrom("WDTT_STANDALONE_MANAGED=0\n"); err == nil {
		t.Fatal("an incomplete probe answer accepted")
	}
	if wdttPlusMayUninstall(wdttPlusLegacyDeploy) == nil || wdttPlusMayUninstall(wdttPlusPreservedData) != nil ||
		wdttPlusMayUninstall(wdttPlusUninstalledData) != nil || !strings.Contains(wdttPlusMarkFor(wdttPlusUninstalledData), "chmod 600") {
		t.Fatal("removal rules")
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

// The data step of the removal stops without /etc/wdtt instead of carrying on as the original does.
func TestWdttPlusUninstallKeepsDataStep(t *testing.T) {
	steps := wdttPlusUninstallSteps(validWdttPlusServer())
	data := steps[3]
	if data.title != "Сохранение данных" {
		t.Fatal(data.title)
	}
	empty := t.TempDir()
	if out := wdttPlusRoot(t, empty, data.command+" && echo STEP_OK"); strings.Contains(out, "STEP_OK") {
		t.Fatalf("data step passed without data: %s", out)
	}
	root := t.TempDir()
	wdttPlusFiles(t, root, "etc/wdtt/passwords.json", "etc/wdtt/wg-keys.dat")
	if out := wdttPlusRoot(t, root, data.command+" && echo STEP_OK"); !strings.Contains(out, "STEP_OK") {
		t.Fatalf("data step failed: %s", out)
	}
	if !strings.Contains(steps[2].command, "--dport 56000 ") || !strings.Contains(steps[2].command, "-t nat -L POSTROUTING") {
		t.Fatal("firewall cleanup")
	}
	clean := "WDTT_UNINSTALL_SERVICE=0\nWDTT_UNINSTALL_BINARY=0\nWDTT_UNINSTALL_INTERFACE=0\nWDTT_UNINSTALL_PROCESS=0\n" +
		"WDTT_UNINSTALL_SYSCTL=0\nWDTT_UNINSTALL_FIREWALL=0\nWDTT_UNINSTALL_DATA=preserved\n"
	if left := wdttPlusUninstallLeft(clean); len(left) != 0 {
		t.Fatal(left)
	}
	if left := wdttPlusUninstallLeft(strings.Replace(clean, "DATA=preserved", "DATA=invalid", 1)); len(left) != 1 {
		t.Fatal("kept data without the marker accepted")
	}
}

// wdttPlusSystemd is a fake systemctl keeping the wdtt unit's state in files.
func wdttPlusSystemd(t *testing.T, root string) []string {
	t.Helper()
	wdttPlusWrite(t, root, "fakebin/systemctl", `#!/bin/bash
state="$FAKE_SYSTEMD"
case "$1" in
  is-active) [ -f "$state/active" ] ;;
  is-enabled) [ -f "$state/enabled" ] ;;
  stop) [ ! -f "$state/fail_stop" ] && rm -f "$state/active" ;;
  start|restart) [ ! -f "$state/fail_start" ] && touch "$state/active" ;;
  enable) touch "$state/enabled" ;;
  disable) rm -f "$state/enabled" ;;
  daemon-reload) ;;
  show) echo loaded ;;
  *) exit 1 ;;
esac
`, 0o755)
	if _, err := osexec.LookPath("flock"); err != nil {
		t.Skip("flock is not available")
	}
	for _, dir := range []string{"systemd", "run/lock", "var/tmp"} {
		if err := os.MkdirAll(filepath.Join(root, dir), 0o700); err != nil {
			t.Fatal(err)
		}
	}
	wdttPlusWrite(t, root, "systemd/active", "", 0o600)
	wdttPlusWrite(t, root, "systemd/enabled", "", 0o600)
	return []string{"PATH=" + filepath.Join(root, "fakebin") + ":" + os.Getenv("PATH"), "FAKE_SYSTEMD=" + filepath.Join(root, "systemd")}
}

func wdttPlusTestPaths(root string) wdttPlusUpdatePaths {
	return wdttPlusUpdatePaths{backup: root + "/var/tmp/wdtt-plus-update-backup", config: root + "/etc/wdtt",
		binary: root + "/usr/local/bin/wdtt-server", unit: root + "/etc/systemd/system/wdtt.service", lock: root + "/run/lock/wdtt-plus-update.lock"}
}

func wdttPlusRead(t *testing.T, path string) string {
	t.Helper()
	data, _ := os.ReadFile(path)
	return string(data)
}

func TestWdttPlusFailedUpdateIsRolledBack(t *testing.T) {
	root := t.TempDir()
	env := wdttPlusSystemd(t, root)
	wdttPlusInstalled(t, root, wdttPlusTestUnit)
	paths := wdttPlusTestPaths(root)
	run := func(script string) (string, error) { return wdttPlusShell(t, script, env...) }

	if out, err := run(paths.prepare("a1")); err != nil || !strings.Contains(out, "WDTT_UPDATE_BACKUP=ready") {
		t.Fatalf("prepare: %v\n%s", err, out)
	}
	if _, err := os.Stat(filepath.Join(root, "systemd/active")); !os.IsNotExist(err) {
		t.Fatal("the service ran during the copy")
	}
	if out, _ := run(paths.probe()); wdttPlusBackupStatus(out) != "prepared_valid" {
		t.Fatalf("probe %s", out)
	}
	if out, err := run(paths.prepare("a2")); err == nil || wdttPlusMarker(out, "WDTT_UPDATE_BACKUP") != "stale" {
		t.Fatalf("a second backup over the first: %s", out)
	}
	ran := filepath.Join(root, "ran")
	if _, err := run(paths.guarded("touch "+quote(ran), "someone-else")); err == nil {
		t.Fatal("another attempt's guard passed")
	}
	if _, err := os.Stat(ran); !os.IsNotExist(err) {
		t.Fatal("ran under another attempt's backup")
	}
	broken := "echo new > \"$BINARY\"; rm \"$CONFIG/passwords.json\"; echo x > \"$CONFIG/extra\"; exit 7"
	if _, err := run(paths.guarded(broken, "a1")); err == nil {
		t.Fatal("the failing update succeeded")
	}
	if out, _ := run(paths.owned("someone-else")); strings.Contains(out, "OWNED=1") {
		t.Fatal("foreign attempt owns the backup")
	}
	if out, _ := run(paths.owned("a1")); !strings.Contains(out, "WDTT_UPDATE_ATTEMPT_OWNED=1") {
		t.Fatalf("owned %s", out)
	}
	if out, err := run(paths.rollback("a1")); err != nil || wdttPlusMarker(out, "WDTT_ROLLBACK") != "ok" {
		t.Fatalf("rollback: %v\n%s", err, out)
	}
	if wdttPlusRead(t, paths.binary) != "#!/bin/sh\necho 20\n" || wdttPlusRead(t, paths.config+"/passwords.json") != wdttPlusTestDB {
		t.Fatal("not restored")
	}
	if _, err := os.Stat(paths.config + "/extra"); !os.IsNotExist(err) {
		t.Fatal("the failed update's file kept")
	}
	if _, err := os.Stat(filepath.Join(root, "systemd/active")); err != nil {
		t.Fatal("the service was not started again")
	}
	if out, _ := run(paths.probe()); wdttPlusBackupStatus(out) != "none" {
		t.Fatalf("backup left: %s", out)
	}
}

func TestWdttPlusVerifiedUpdateDropsBackup(t *testing.T) {
	root := t.TempDir()
	env := wdttPlusSystemd(t, root)
	wdttPlusInstalled(t, root, wdttPlusTestUnit)
	paths := wdttPlusTestPaths(root)
	run := func(script string) (string, error) { return wdttPlusShell(t, script, env...) }
	if out, err := run(paths.prepare("b1")); err != nil {
		t.Fatalf("prepare: %s", out)
	}
	if out, err := run(paths.guarded("echo new > \"$BINARY\"; chmod 755 \"$BINARY\"; systemctl start wdtt; echo APPLIED", "b1")); err != nil || !strings.Contains(out, "APPLIED") {
		t.Fatalf("guarded update: %v\n%s", err, out)
	}
	if out, err := run(paths.cleanup("b1")); err != nil || wdttPlusMarker(out, "WDTT_UPDATE_CLEANUP") != "ok" {
		t.Fatalf("cleanup: %v\n%s", err, out)
	}
	if wdttPlusRead(t, paths.binary) != "new\n" {
		t.Fatal("the verified update was undone")
	}
	if out, _ := run(paths.probe()); wdttPlusBackupStatus(out) != "none" {
		t.Fatalf("backup left: %s", out)
	}
}

func TestWdttPlusDamagedBackupIsNotRestored(t *testing.T) {
	root := t.TempDir()
	env := wdttPlusSystemd(t, root)
	wdttPlusInstalled(t, root, wdttPlusTestUnit)
	paths := wdttPlusTestPaths(root)
	run := func(script string) (string, error) { return wdttPlusShell(t, script, env...) }
	if out, err := run(paths.prepare("c1")); err != nil {
		t.Fatalf("prepare: %s", out)
	}
	wdttPlusWrite(t, root, "var/tmp/wdtt-plus-update-backup/wdtt-server", "tampered", 0o755)
	out, _ := run(paths.probe())
	if wdttPlusBackupStatus(out) != "prepared_corrupted" || wdttPlusMarker(out, "WDTT_UPDATE_BACKUP_DIAGNOSTIC") != "checksum_mismatch" {
		t.Fatalf("probe %s", out)
	}
	if out, _ := run(paths.rollback("c1")); strings.Contains(out, "WDTT_ROLLBACK=ok") || wdttPlusRead(t, paths.binary) == "tampered" {
		t.Fatal("restored from a damaged backup")
	}
}

// A service that will not stop ends the preparation: it is started again and the copy moved aside.
func TestWdttPlusFailedPreparationRestartsService(t *testing.T) {
	root := t.TempDir()
	env := wdttPlusSystemd(t, root)
	wdttPlusInstalled(t, root, wdttPlusTestUnit)
	wdttPlusWrite(t, root, "systemd/fail_stop", "", 0o600)
	paths := wdttPlusTestPaths(root)
	out, err := wdttPlusShell(t, paths.prepare("d1"), env...)
	if err == nil || strings.Contains(out, "WDTT_UPDATE_BACKUP=ready") || !strings.Contains(out, "stop_service") {
		t.Fatalf("prepare passed a running service: %s", out)
	}
	if _, err := os.Stat(filepath.Join(root, "systemd/active")); err != nil {
		t.Fatal("the service is down after the failed preparation")
	}
	if out, _ := wdttPlusShell(t, paths.probe(), env...); wdttPlusBackupStatus(out) != "none" {
		t.Fatalf("the failed copy blocks the next update: %s", out)
	}
}

func wdttPlusTestDatabase(t *testing.T, text string) wdttPlusDB {
	t.Helper()
	db, err := wdttPlusParseDB(text)
	if err != nil {
		t.Fatal(err)
	}
	return db
}

const wdttPlusClientsDB = `{"main_password":"old-password","admin_down_bytes":10,"dns":"8.8.8.8","bot_token":"keep",
 "passwords":{"client-one":{"label":"Phone","device_id":"dev1","down_bytes":100,"up_bytes":5,"expires_at":0,
   "traffic":[{"date":"2026-10-01","down_bytes":100,"up_bytes":5}],
   "bind_history":[{"status":"active","device_id":"dev1","bound_at":1}]},
  "expired-one":{"label":"Old","expires_at":1000,"purge_after":2000}},
 "devices":{"dev1":{"device_id":"dev1","ip":"10.66.66.2","pub_key":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="}},
 "custom_field":{"x":12345678901234567890}}`

func TestWdttPlusDatabaseKeepsServerData(t *testing.T) {
	s := validWdttPlusServer()
	data, err := wdttPlusDatabase(wdttPlusTestDatabase(t, wdttPlusClientsDB), s)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(data), "12345678901234567890") {
		t.Fatal("a large number was rounded")
	}
	db := wdttPlusTestDatabase(t, string(data))
	if db["main_password"] != "main-password" || db["dns"] != "1.1.1.1" || db["default_ports"] != "56000,56001,9000" || db["bot_token"] != "keep" {
		t.Fatalf("settings %v", db)
	}
	if err := wdttPlusValidatePreserved(wdttPlusTestDatabase(t, wdttPlusClientsDB), db, time.Unix(1_800_000_000, 0)); err != nil {
		t.Fatal(err)
	}
}

func TestWdttPlusDatabaseChecks(t *testing.T) {
	for _, bad := range []string{
		`{"main_password":"x","passwords":[],"devices":{}}`,
		`{"main_password":"x","passwords":{"p":{"device_id":"missing"}},"devices":{}}`,
		`{"main_password":"x","passwords":{"p":{"down_bytes":-1}},"devices":{}}`,
		`{"main_password":"x","passwords":{"p":{"expires_at":10,"purge_after":5}},"devices":{}}`,
		`{"main_password":"x","passwords":{},"devices":{},"default_ports":"1,2"}`,
		`{"main_password":"x","passwords":{},"devices":{"d":{"ip":"192.168.1.2"}}}`,
		`{"main_password":"x","passwords":{},"devices":{},"public_ip":"http://host"}`,
		`{"main_password":"x","passwords":{},"devices":{},"max_passwords":501}`,
	} {
		if wdttPlusValidateDB(wdttPlusTestDatabase(t, bad)) == nil {
			t.Fatalf("accepted %s", bad)
		}
	}
	if _, err := wdttPlusParseDB(`{"a":1} {"b":2}`); err == nil {
		t.Fatal("trailing data accepted")
	}
	blank := wdttPlusTestDatabase(t, `{"main_password":"","passwords":{},"devices":{}}`)
	if wdttPlusValidateForPreserving(blank, "main-password") != nil || wdttPlusValidateForPreserving(blank, "") == nil {
		t.Fatal("blank main password recovery")
	}
	if _, err := wdttPlusParseRead("WDTT_ERROR=unsafe_passwords_json\n"); err == nil {
		t.Fatal("unsafe database accepted")
	}
	if db, err := wdttPlusParseRead("\n"); db != nil || err != nil {
		t.Fatal("no database")
	}
}

func TestWdttPlusLostClientsFailTheUpdate(t *testing.T) {
	now := time.Unix(1_800_000_000, 0)
	before := wdttPlusTestDatabase(t, wdttPlusClientsDB)
	for name, change := range map[string]func(db wdttPlusDB){
		"lost client": func(db wdttPlusDB) { delete(db["passwords"].(map[string]any), "client-one") },
		"counter": func(db wdttPlusDB) {
			db["passwords"].(map[string]any)["client-one"].(map[string]any)["down_bytes"] = json.Number("1")
		},
		"label": func(db wdttPlusDB) {
			db["passwords"].(map[string]any)["client-one"].(map[string]any)["label"] = "Other"
		},
		"binding": func(db wdttPlusDB) { db["passwords"].(map[string]any)["client-one"].(map[string]any)["device_id"] = "" },
		"traffic history": func(db wdttPlusDB) {
			db["passwords"].(map[string]any)["client-one"].(map[string]any)["traffic"] = []any{}
		},
		"bind history": func(db wdttPlusDB) {
			db["passwords"].(map[string]any)["client-one"].(map[string]any)["bind_history"] = []any{}
		},
		"owner traffic": func(db wdttPlusDB) { db["admin_down_bytes"] = json.Number("0") },
	} {
		after := wdttPlusTestDatabase(t, wdttPlusClientsDB)
		change(after)
		if wdttPlusValidatePreserved(before, after, now) == nil {
			t.Fatalf("%s accepted", name)
		}
	}
	// A client long past its term may be purged by the server.
	after := wdttPlusTestDatabase(t, wdttPlusClientsDB)
	delete(after["passwords"].(map[string]any), "expired-one")
	if err := wdttPlusValidatePreserved(before, after, now); err != nil {
		t.Fatal(err)
	}
	full := make([]any, 0, 50)
	for i := 0; i < 49; i++ {
		full = append(full, map[string]any{"status": "unbound", "bound_at": json.Number("2")})
	}
	if !wdttPlusBindHistoryKept([]any{map[string]any{"status": "old"}, full[0]}, append(full, map[string]any{"status": "new"})) {
		t.Fatal("history shifted by the server's limit of 50")
	}
}

func TestWdttPlusVerifyNeedsTheNewServer(t *testing.T) {
	sha := strings.Repeat("ab", 32)
	good := "BINARY=1\nCONFIG=1\nSERVICE=active\nADMIN_SOCKET=1\nSERVER_SHA256=" + sha + "\nSERVER_VERSION=20\n"
	if missing := wdttPlusVerifyFailures(good, sha); len(missing) != 0 {
		t.Fatal(missing)
	}
	if missing := wdttPlusVerifyFailures(good, strings.Repeat("cd", 32)); len(missing) != 1 {
		t.Fatal("an old binary accepted")
	}
	if missing := wdttPlusVerifyFailures(strings.Replace(good, "SERVER_VERSION=20", "SERVER_VERSION=", 1), sha); len(missing) != 1 {
		t.Fatal("no version accepted")
	}
}

func wdttPlusFakeAnswers(probe string) func(string) (string, uint32) {
	return func(command string) (string, uint32) {
		switch {
		case command == "uname -m":
			return "x86_64\n", 0
		case strings.Contains(command, "WDTT_ROOT_ACCESS"):
			return "WDTT_ROOT_ACCESS=ok\n", 0
		case strings.Contains(command, "root_prefix="):
			return probe, 0
		case strings.Contains(command, "Preserved by WDTT Plus Android deploy"):
			return "WDTT_PRESERVED_MARKER=ready\n", 0
		case strings.Contains(command, "WDTT_UNINSTALL_SERVICE"):
			return "WDTT_UNINSTALL_SERVICE=0\nWDTT_UNINSTALL_BINARY=0\nWDTT_UNINSTALL_INTERFACE=0\nWDTT_UNINSTALL_PROCESS=0\n" +
				"WDTT_UNINSTALL_SYSCTL=0\nWDTT_UNINSTALL_FIREWALL=0\nWDTT_UNINSTALL_DATA=preserved\n", 0
		}
		return "", 0
	}
}

func wdttPlusProbeAnswer(flag string) string {
	out := ""
	for _, name := range []string{"WDTT_STANDALONE_MANAGED", "WDTT_ANDROID_DEPLOY_MANAGED", "WDTT_LEGACY_ANDROID_DEPLOY_CANDIDATE",
		"WDTT_ANDROID_DATA_PRESERVED", "WDTT_INCOMPLETE_ANDROID_DEPLOY_CANDIDATE"} {
		value := "0"
		if name == flag {
			value = "1"
		}
		out += name + "=" + value + "\n"
	}
	return out + "WDTT_INSTALL_TRACE=1\n"
}

func wdttPlusRunFake(t *testing.T, f *fakeSSH, operation string) (string, error) {
	t.Helper()
	s := validWdttPlusServer()
	s["host"], s["sshPort"], s["hostKey"] = f.host, f.port, f.fingerprint
	request, _ := json.Marshal(Request{Operation: operation, Server: s})
	var output bytes.Buffer
	err := Run(strings.NewReader(string(request)+"\n"), &output)
	return output.String(), err
}

func TestWdttPlusUninstallMarksDataFirst(t *testing.T) {
	f := startFakeSSH(t, wdttPlusFakeAnswers(wdttPlusProbeAnswer("WDTT_ANDROID_DEPLOY_MANAGED")))
	if out, err := wdttPlusRunFake(t, f, "uninstall"); err != nil {
		t.Fatalf("%v %s", err, out)
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	marker, stop := -1, -1
	for i, command := range f.commands {
		if strings.Contains(command, "Preserved by WDTT Plus Android deploy") && marker < 0 {
			marker = i
		}
		if strings.Contains(command, "systemctl stop wdtt") && stop < 0 {
			stop = i
		}
	}
	if marker < 0 || stop < 0 || marker > stop {
		t.Fatalf("the data must be marked before the service is removed: %d %d", marker, stop)
	}
}

func TestWdttPlusStandaloneIsLeftAlone(t *testing.T) {
	for _, operation := range []string{"uninstall", "deploy"} {
		f := startFakeSSH(t, wdttPlusFakeAnswers(wdttPlusProbeAnswer("WDTT_STANDALONE_MANAGED")))
		if out, err := wdttPlusRunFake(t, f, operation); err == nil || !strings.Contains(out, "server-installer") {
			t.Fatalf("%s: %v %s", operation, err, out)
		}
		f.mu.Lock()
		if len(f.commands) != 3 {
			t.Fatalf("%s ran more than the checks: %v", operation, f.commands)
		}
		f.mu.Unlock()
	}
}

// After the user stops a deploy the rollback runs on a new connection with the trusted key.
func TestWdttPlusRecoveryReconnectsAfterStop(t *testing.T) {
	answers := map[string]string{"WDTT_UPDATE_ATTEMPT_OWNED": "WDTT_UPDATE_ATTEMPT_OWNED=1\n", "WDTT_ROLLBACK=ok": "WDTT_ROLLBACK=ok\n",
		"wdtt_backup_result none absent": "WDTT_UPDATE_BACKUP_STATUS=prepared_valid\n"}
	f := startFakeSSH(t, func(command string) (string, uint32) {
		if strings.Contains(command, "wdtt_update_phase rolling_back") {
			return answers["WDTT_ROLLBACK=ok"], 0
		}
		if strings.Contains(command, "WDTT_UPDATE_ATTEMPT_OWNED") {
			return answers["WDTT_UPDATE_ATTEMPT_OWNED"], 0
		}
		return answers["wdtt_backup_result none absent"], 0
	})
	s := validWdttPlusServer()
	s["host"], s["sshPort"], s["hostKey"] = f.host, f.port, f.fingerprint
	ctx, cancel := context.WithCancel(context.Background())
	p := &protocol{output: json.NewEncoder(io.Discard), ctx: ctx}
	client, err := p.dial(s, false)
	if err != nil {
		t.Fatal(err)
	}
	cancel()
	restored, err := p.wdttPlusRecover(client, s, "attempt")
	if err != nil || !restored {
		t.Fatalf("rollback after stop: %v %v", restored, err)
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	if len(f.commands) != 3 || !strings.Contains(f.commands[2], "wdtt_update_phase rolling_back") {
		t.Fatalf("commands %d", len(f.commands))
	}
}
