package relay

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func TestTrimIndentIsKotlins(t *testing.T) {
	cases := map[string]string{
		"\n    a\n      b\n    c\n":           "a\n  b\nc",
		"\n    a\n\n    b\n  ":                "a\n\nb",
		"a\n  b":                              "a\n  b",
		"\n        x\n    ":                   "x",
		"   \n   ":                            "",
		"\n    if x; then\n      y\n    fi\n": "if x; then\n  y\nfi",
	}
	for in, want := range cases {
		if got := trimIndent(in); got != want {
			t.Fatalf("trimIndent(%q) = %q, want %q", in, got, want)
		}
	}
	if got := shellScript("\n    a\n", "\n  b\n\n"); got != "a\nb\n" {
		t.Fatalf("shellScript %q", got)
	}
}

func composedExitScripts(t *testing.T) map[string]string {
	t.Helper()
	must := func(script string, err error) string {
		t.Helper()
		if err != nil {
			t.Fatal(err)
		}
		return script
	}
	socks, _ := proxyKindByName("Socks5")
	return map[string]string{
		"status": outboundStatusScript(), "snapshot": outboundSnapshotScript(), "direct": disableOutboundExitScript(),
		"diagnostics": shellScript(outboundStatusScript(), exitScript(13)), "tun-candidates": tunInterfaceCandidatesScript(),
		"tun-enable": must(buildTunInterfaceExitScript("tun0")), "tun-check": must(checkTunInterfaceExitScript("tun0")),
		"tun-delete": deleteTunInterfaceExitScript(), "firewall": must(localProxyFirewallScript(1080)),
		"local-install": must(buildLocalProxyInstallScript(1080, "user", "password1")), "local-check": must(checkLocalProxyScript(1080, "user", "password1")),
		"local-stop": stopLocalProxyScript(), "local-remove": removeLocalProxyScript(),
		"external-check":  must(checkExternalProxyScript(socks, "proxy.example.com", 1080, "u", "p'w")),
		"external-enable": must(enableExternalProxyScript(socks, "proxy.example.com", 1080, "u", `p"w`)),
		"external-routes": externalProxyRoutesScript(), "external-delete": deleteExternalProxyScript(),
		"warp-install": must(buildFreeWarpInstallScript(1280)), "warp-check": checkFreeWarpScript(true),
		"warp-update": shellScript(outboundShellPrelude(), wgcfToolManagementScript(), exitScript(31)),
		"warp-reset":  shellScript(outboundShellPrelude(), exitScript(36)), "warp-delete": shellScript(outboundShellPrelude(), exitScript(37)),
		"wg-enable": shellScript(outboundShellPrelude(), exitScript(38), wireGuardPolicyScript("imported_wg", "VPN/WireGuard-файл")),
		"wg-delete": deleteImportedWireGuardScript(), "wg-check": checkWireGuardExitScript("imported_wg"),
		"vps-current-delete": deleteVpsCurrentScript(), "vps-foreign-delete": deleteVpsForeignScript(),
		"vps-previous": trimIndent(exitScript(42)), "vps-keys": shellScript(outboundShellPrelude(), exitScript(43)),
		"vps-foreign": shellScript(exitScript(44, quote("pub"), "51820", quote("/etc/wdtt-plus/wg-exit/transactions/x/foreign")), outboundShellPrelude(), exitScript(45)),
		"vps-current": shellScript(exitScript(46, quote("pub"), quote("203.0.113.9"), "51820", quote("/etc/wdtt-plus/wg-exit/transactions/x/current")),
			outboundShellPrelude(), exitScript(47), wireGuardPolicyScript("wireguard_vps", "другой сервер 203.0.113.9:51820")),
		"vps-rollback-foreign": shellScript("BACKUP_DIR="+quote("/tmp/x"), exitScript(48)),
		"vps-rollback-current": shellScript("BACKUP_DIR="+quote("/tmp/x"), outboundShellPrelude(), exitScript(49, quote("/tmp/x"))),
		"profile":              outboundProfileSaveScript(outboundProfileForms{LocalProxyPort: "1080"}),
	}
}

func TestExitScriptsAreComplete(t *testing.T) {
	scripts := composedExitScripts(t)
	bash, bashErr := exec.LookPath("bash")
	dir := t.TempDir()
	for name, script := range scripts {
		if strings.Contains(script, "⟦") || strings.Contains(script, "${'$'}") {
			t.Fatalf("%s keeps a template marker", name)
		}
		if bashErr != nil {
			continue
		}
		path := filepath.Join(dir, name+".sh")
		os.WriteFile(path, []byte(script), 0600)
		if out, err := exec.Command(bash, "-n", path).CombinedOutput(); err != nil {
			t.Fatalf("%s is not valid shell: %v\n%s", name, err, out)
		}
	}
	prelude := outboundShellPrelude()
	if !strings.HasPrefix(prelude, "set -e\numask 077\n") || !strings.Contains(prelude, "\nwdtt_probe_ipv4() {\n") || !strings.Contains(prelude, "\nwdtt_test_redsocks_path() {\n") {
		t.Fatal("the prelude is not WDTT Plus's, flush left with both probe libraries")
	}
	if !strings.Contains(scripts["vps-current"], "FOREIGN_PUB='pub'") || !strings.Contains(scripts["warp-install"], "WARP_MTU=1280\nset -e") {
		t.Fatal("interpolated values are not where WDTT Plus puts them")
	}
	if !strings.Contains(scripts["profile"], "WG_VPS_PASSWORD_B64=\n") {
		t.Fatal("the second VPS password must never be written to the server")
	}
}

func TestWireGuardFileValidation(t *testing.T) {
	good := "[Interface]\nPrivateKey = abc\nAddress = 10.0.0.2/32\nDNS = 1.1.1.1\nMTU = 1420\n[Peer]\nPublicKey = def\nEndpoint = vpn.example.com:51820\nAllowedIPs = 0.0.0.0/0, ::/0\n"
	if err := validateWireGuardConfigText(good); err != nil {
		t.Fatal(err)
	}
	for name, bad := range map[string]string{
		"hook":         strings.Replace(good, "MTU = 1420", "PostUp = iptables -F", 1),
		"two peers":    good + "[Peer]\nPublicKey = x\nEndpoint = a:1\nAllowedIPs = 0.0.0.0/0\n",
		"no default":   strings.Replace(good, "0.0.0.0/0, ", "", 1),
		"bad endpoint": strings.Replace(good, ":51820", "", 1),
		"bad mtu":      strings.Replace(good, "1420", "100", 1),
		"unknown key":  strings.Replace(good, "MTU = 1420", "SaveConfig = true", 1),
	} {
		if validateWireGuardConfigText(bad) == nil {
			t.Fatalf("%s accepted", name)
		}
	}
	clean, err := sanitizeWireGuardConfigForWdttExit(good)
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(clean, "DNS") || !strings.Contains(clean, "MTU = 1420\nTable = off\n[Peer]") {
		t.Fatalf("sanitized config:\n%s", clean)
	}
}

func TestExitInputChecks(t *testing.T) {
	if localProxyCredentialsIssue("user", "password1") != "" || localProxyCredentialsIssue("us er", "password1") == "" || localProxyCredentialsIssue("user", "short") == "" {
		t.Fatal("local proxy credentials")
	}
	if externalProxyCredentialsIssue("", "") != "" || externalProxyCredentialsIssue("a:b", "p") == "" || externalProxyCredentialsIssue("", "p") == "" {
		t.Fatal("external proxy credentials")
	}
	for _, name := range []string{"", "wdtt0", "lo", "averyveryverylongname", "-tun"} {
		if tunInterfaceNameIssue(name) == "" {
			t.Fatalf("interface %q accepted", name)
		}
	}
	for host, ok := range map[string]bool{"203.0.113.5": true, "proxy.example.com": true, "localhost": false, "a..b": false, "h:80": false} {
		if isValidPublicHost(host) != ok {
			t.Fatalf("host %q", host)
		}
	}
	if _, err := buildFreeWarpInstallScript(1000); err == nil {
		t.Fatal("WARP MTU below 1280 accepted")
	}
}

func TestOutboundSnapshotAndWarnings(t *testing.T) {
	output := "noise\nWDTT_OUTBOUND_MODE=external_proxy\nWDTT_EXTERNAL_PROXY_SERVICE_ACTIVE=1\nWDTT_EXTERNAL_PROXY_ROUTE_ACTIVE=0\n" +
		"WDTT_EXTERNAL_PROXY_HOST_B64=" + b64Text("proxy.example.com") + "\nWDTT_EXTERNAL_PROXY_KIND_NAME=Bogus\n"
	s := parseOutboundServerSnapshot(output)
	if s.t("mode") != "external_proxy" || s.t("externalProxyHost") != "proxy.example.com" || s.t("externalProxyKindName") != "" {
		t.Fatalf("snapshot %v", s)
	}
	if !strings.Contains(s.warning(), "правило перенаправления трафика WDTT отсутствует") {
		t.Fatalf("warning %q", s.warning())
	}
	conflict := parseOutboundServerSnapshot("WDTT_EXTERNAL_PROXY_ROUTE_ACTIVE=1\nWDTT_WG_NAT_ACTIVE=1\n")
	if conflict["routeConflict"] == nil || !strings.Contains(conflict.warning(), "внешний TCP-прокси и WireGuard-выход") {
		t.Fatalf("conflict %v", conflict)
	}
	if direct := parseOutboundServerSnapshot(""); direct.t("mode") != "direct" || direct.warning() != "" || direct.forms().WireGuardExitHost != "" {
		t.Fatalf("empty snapshot %v", direct)
	}
	list := parseTunInterfaceCandidates("WDTT_TUN_CANDIDATE=tun1|0\nWDTT_TUN_CANDIDATE=wg0|1\nWDTT_TUN_CANDIDATE=tun0|1\nWDTT_TUN_CANDIDATE=wdtt0|1\nWDTT_TUN_CANDIDATE=tun0|0\n")
	if len(list) != 3 || list[0].Name != "tun0" || list[1].Name != "wg0" || list[2].Name != "tun1" || list[2].Up {
		t.Fatalf("candidates %v", list)
	}
	if got := rootScriptErrorExcerpt("log\nWDTT_ERROR=first\nmore\nWDTT_ERROR=second\ntail"); got != "WDTT_ERROR=second\ntail" {
		t.Fatalf("excerpt %q", got)
	}
}
