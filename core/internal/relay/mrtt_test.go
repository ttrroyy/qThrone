package relay

import (
	"strings"
	"testing"
)

func TestMaxPageReadsLikeTheOriginal(t *testing.T) {
	for raw, want := range map[string]string{
		"https://web.max.ru/-80123":          "-80123",
		"https://web.max.ru/chat/-5":         "-5",
		"https://web.max.ru/c/77?x=1":        "77",
		"https://web.max.ru/?chat_id=-9":     "-9",
		"https://web.max.ru/":                "",
		"https://web.max.ru/settings":        "",
		"http://web.max.ru/-1":               "",
		"https://web.max.ru.evil.example/-1": "",
		"https://user@web.max.ru/-1":         "",
		"https://web.max.ru:444/-1":          "",
	} {
		if got := maxGroupFromURL(raw); got != want {
			t.Errorf("%s: %q, want %q", raw, got, want)
		}
	}
	if validMaxToken("short") || validMaxToken(strings.Repeat("a", 31)+" b") || !validMaxToken(strings.Repeat("a", 32)) {
		t.Fatal("token check")
	}
}

func TestMRTTDeployMarkers(t *testing.T) {
	if !mrttDeployed("x\nMRTT_DEPLOY_OK\n") || mrttDeployed("not MRTT_DEPLOY_OK now") {
		t.Fatal("success marker")
	}
	for output, want := range map[string]string{
		"MRTT_DEPLOY_ROLLED_BACK\nMRTT_DEPLOY_ERROR|step|порт занят": "восстановлена Причина: порт занят",
		"MRTT_DEPLOY_ROLLBACK_FAILED":                                "требуется проверка VPS",
	} {
		if got := mrttDeployFailure(1, output); !strings.Contains(got, want) {
			t.Fatal(got)
		}
	}
	if got := mrttDeployFailure(20, ""); !strings.Contains(got, "работающий сервер не останавливался") {
		t.Fatal(got)
	}
	if got := mrttDeployFailure(2, ""); !strings.Contains(got, "отклонил параметры") {
		t.Fatal(got)
	}
}

func TestMRTTServerChecks(t *testing.T) {
	server := func() map[string]string {
		return map[string]string{"kind": "mrtt", "host": "1.2.3.4", "user": "root", "sshPort": "22", "peerPort": "46000", "webPort": "46002", "sshPassword": "x", "password": "Паролька1"}
	}
	if e := validateServer(server(), true); e != nil {
		t.Fatal(e)
	}
	for name, change := range map[string]func(map[string]string){
		"ports":    func(s map[string]string) { s["webPort"] = "46000" },
		"password": func(s map[string]string) { s["password"] = "a|b" },
		"space":    func(s map[string]string) { s["password"] = "a b" },
		"host":     func(s map[string]string) { s["host"] = "1.2.3.4:22" },
	} {
		s := server()
		change(s)
		if validateServer(s, true) == nil {
			t.Fatal(name)
		}
	}
	if asset, e := mrttServerAsset("aarch64\n"); e != nil || asset != "mrtt-linux-arm64" {
		t.Fatal(asset, e)
	}
	if _, e := mrttServerAsset("ppc64"); e == nil {
		t.Fatal("unsupported machine")
	}
}

func TestMRTTResponseAndPath(t *testing.T) {
	if body, e := parseMRTTResponse([]byte("HTTP/1.0 200 OK\r\nA: b\r\n\r\n[1]")); e != nil || body != "[1]" {
		t.Fatal(body, e)
	}
	if _, e := parseMRTTResponse([]byte("HTTP/1.0 403 X\r\n\r\nнет")); e == nil || e.Error() != "нет" {
		t.Fatal(e)
	}
	if _, e := parseMRTTResponse([]byte("garbage")); e == nil {
		t.Fatal("garbage accepted")
	}
	if got := mrttPathEscape("a b/ю"); got != "a%20b%2F%D1%8E" {
		t.Fatal(got)
	}
}

func TestMRTTBatchTakesOnlyApiReads(t *testing.T) {
	if !mrttBatchValid([]string{"/api/clients", "/api/groups"}) {
		t.Fatal("api paths refused")
	}
	for _, paths := range [][]string{nil, {"/etc/passwd"}, {"/api/x y"}, {"/api/a\nHost: x"}, make([]string, 17)} {
		if mrttBatchValid(paths) {
			t.Fatal(paths)
		}
	}
}
