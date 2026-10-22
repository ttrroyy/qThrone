package relay

import (
	"bytes"
	"context"
	"errors"
	"github.com/zalando/go-keyring"
	"io"
	"net/http"
	"net/url"
	"strings"
	"testing"
	"time"
)

func TestVKTokenDoesNotOpenBrowserAgainAfterConfirmation(t *testing.T) {
	keyring.MockInit()
	dir := t.TempDir()
	t.Setenv("APPDATA", dir)
	t.Setenv("XDG_CONFIG_HOME", dir)
	t.Setenv("HOME", dir)
	p := &protocol{ctx: context.Background(), vkBrowserUsed: true}
	a := account{}
	if err := p.tokenForCalls(&a); err == nil || !strings.Contains(err.Error(), "Повторное окно") {
		t.Fatalf("expected a bounded authorization error, got %v", err)
	}
}

func TestCallsTokenReuseAndHTTPFallback(t *testing.T) {
	now := time.Now().Unix()
	a := account{Token: "cached", Expires: now + 3600}
	called := false
	if err := obtainVKToken(&a, now, func() error { t.Fatal("valid token refreshed"); return nil }, func() error { t.Fatal("valid token opened browser"); return nil }); err != nil {
		t.Fatal(err)
	}
	a.Expires = 0
	if err := obtainVKToken(&a, now, func() error { return errors.New("HTTP unavailable") }, func() error { called = true; a.Token = "browser"; return nil }); err != nil || !called || a.Token != "browser" {
		t.Fatal("HTTP failure did not fall back to the shared account browser")
	}
	called = false
	if err := obtainVKToken(&a, now, func() error { return vkError{Code: 6} }, func() error { called = true; return nil }); err == nil || called {
		t.Fatal("rate limit must not be retried through browser")
	}
}

func TestAutoHashesRefreshRejectedTokenOnce(t *testing.T) {
	keyring.MockInit()
	dir := t.TempDir()
	t.Setenv("APPDATA", dir)
	t.Setenv("XDG_CONFIG_HOME", dir)
	t.Setenv("HOME", dir)
	old := http.DefaultTransport
	defer func() { http.DefaultTransport = old }()
	calls, refreshes := 0, 0
	http.DefaultTransport = roundTripFunc(func(r *http.Request) (*http.Response, error) {
		body := ""
		if r.URL.Host == "oauth.vk.com" {
			refreshes++
			location := "https://oauth.vk.com/blank.html#" + url.Values{"access_token": {"fresh"}, "state": {r.URL.Query().Get("state")}, "user_id": {"42"}}.Encode()
			return &http.Response{StatusCode: 302, Body: io.NopCloser(strings.NewReader("")), Header: http.Header{"Location": {location}}}, nil
		}
		calls++
		if calls == 1 {
			body = `{"error":{"error_code":5}}`
		} else {
			body = `{"response":{"join_link":"https://vk.ru/call/join/fixture_hash_123456789"}}`
		}
		return &http.Response{StatusCode: 200, Body: io.NopCloser(strings.NewReader(body)), Header: make(http.Header)}, nil
	})
	if err := saveAccount(account{Token: "stale", Expires: time.Now().Add(time.Hour).Unix(), User: "42", Cookies: []browserCookie{{Name: "remixsid", Value: "session", Domain: ".vk.ru"}}}); err != nil {
		t.Fatal(err)
	}
	var output bytes.Buffer
	if err := Run(strings.NewReader("{\"operation\":\"vk-hashes\",\"count\":1}\n"), &output); err != nil {
		t.Fatal(err)
	}
	if calls != 2 || refreshes != 1 || strings.Contains(output.String(), "fresh") || strings.Contains(output.String(), "stale") {
		t.Fatal("rejected token retry or redaction changed")
	}
}

func TestBootstrapUsesOnlyUnambiguousApprovedServer(t *testing.T) {
	s := map[string]string{"kind": "wdttplus", "host": "192.0.2.1", "peerPort": "56000", "sshPort": "22", "user": "root", "hostKey": "SHA256:fixture", "sshPassword": "secret"}
	if wdttPlusBootstrapServer("192.0.2.1:56000", []map[string]string{s}) == nil {
		t.Fatal("approved matching VPS not selected")
	}
	if wdttPlusBootstrapServer("192.0.2.1:56000", []map[string]string{s, s}) != nil {
		t.Fatal("ambiguous VPS selected")
	}
	for _, key := range []string{"hostKey", "sshPassword", "user"} {
		copy := map[string]string{}
		for k, v := range s {
			copy[k] = v
		}
		delete(copy, key)
		if wdttPlusBootstrapServer("192.0.2.1:56000", []map[string]string{copy}) != nil {
			t.Fatal("unapproved/incomplete SSH access selected")
		}
	}
}
