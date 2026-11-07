package relay

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"time"
	"unicode"

	"github.com/gorilla/websocket"
	"github.com/zalando/go-keyring"
)

// The MAX binding, as the original MRTT app makes it: the user signs in to web.max.ru and opens a group;
// only the token from __oneme_auth and the group ID from the address are kept. Nothing else of the account is touched.
type maxAccount struct {
	Token string `json:"token"`
	Group string `json:"group"`
	// The browser's own record of the session: a repeated binding starts signed in, as the original keeps its WebView data.
	Auth string `json:"auth"`
}

var maxGroupID = regexp.MustCompile(`^-?[0-9]{1,64}$`)

func maxURL(raw string) bool {
	parsed, e := url.Parse(raw)
	if e != nil || parsed.User != nil || !strings.EqualFold(parsed.Scheme, "https") {
		return false
	}
	host := strings.ToLower(parsed.Hostname())
	return (parsed.Port() == "" || parsed.Port() == "443") && (host == "max.ru" || strings.HasSuffix(host, ".max.ru"))
}

func maxGroupFromURL(raw string) string {
	if !maxURL(raw) {
		return ""
	}
	parsed, _ := url.Parse(raw)
	segments := strings.Split(strings.Trim(parsed.Path, "/"), "/")
	candidate := segments[0]
	if candidate == "chat" || candidate == "chats" || candidate == "c" {
		candidate = ""
		if len(segments) > 1 {
			candidate = segments[1]
		}
	}
	if maxGroupID.MatchString(candidate) {
		return candidate
	}
	values := parsed.Query()
	for _, key := range []string{"chat_id", "chatId", "chat", "chat_id_str"} {
		if value := values.Get(key); maxGroupID.MatchString(value) {
			return value
		}
	}
	return ""
}

func validMaxToken(token string) bool {
	return len(token) >= 32 && len(token) <= 4096 && strings.IndexFunc(token, unicode.IsSpace) < 0
}

const maxReadPage = `(function() {
    try {
        var raw = localStorage.getItem('__oneme_auth'), token = '';
        try { token = raw ? JSON.parse(raw).token : ''; } catch (e) {}
        var app = document.getElementById('app') || document.body;
        var ready = !!(app && app.querySelector('button, input, [role="button"]') && app.innerText.trim().length > 20);
        return JSON.stringify({url: location.href, token: typeof token === 'string' ? token : '', auth: raw || '', ready: ready});
    } catch (e) { return ''; }
})()`

func maxThemeScript(dark bool) string {
	scheme := "light"
	if dark {
		scheme = "dark"
	}
	return `(function() { var scheme = '` + scheme + `'; try {
        var theme = JSON.parse(localStorage.getItem('__oneme_theme') || '{}');
        if (theme.colorScheme !== scheme) { theme.colorScheme = scheme; localStorage.setItem('__oneme_theme', JSON.stringify(theme)); }
    } catch (e) {} document.documentElement.dataset.colorScheme = scheme; document.documentElement.style.colorScheme = scheme; })()`
}

func readMaxAccount() (maxAccount, error) {
	var a maxAccount
	raw, e := secureGet("max")
	if e != nil {
		return a, e
	}
	if e = json.Unmarshal([]byte(raw), &a); e != nil {
		return a, fmt.Errorf("Некорректная привязка MAX")
	}
	return a, nil
}

func maxInfo() (map[string]any, error) {
	a, e := readMaxAccount()
	if errors.Is(e, keyring.ErrNotFound) {
		return map[string]any{"linked": false}, nil
	}
	if e != nil {
		return nil, e
	}
	return map[string]any{"linked": a.Token != "" && a.Group != "", "group": a.Group}, nil
}

// maxLogin opens web.max.ru in a private browser profile. The browser is denied every device prompt:
// camera, microphone and the rest of the computer stay out of reach of this page.
func (p *protocol) maxLogin(r Request) error {
	path := browserPath()
	if path == "" {
		return fmt.Errorf("Браузер для входа в MAX не найден: нужен Chrome, Edge, Chromium, Brave или Яндекс Браузер")
	}
	required := strings.TrimSpace(r.ID)
	if required != "" && !maxGroupID.MatchString(required) {
		return fmt.Errorf("Некорректный ID группы MAX")
	}
	previous, _ := readMaxAccount()
	dir, e := os.MkdirTemp("", "rawline-max-")
	if e != nil {
		return e
	}
	defer os.RemoveAll(dir)
	// As long as the original waits for a sign-in.
	ctx, cancel := context.WithTimeout(p.ctx, 12*time.Minute)
	defer cancel()
	command := exec.CommandContext(ctx, path, "--user-data-dir="+dir, "--remote-debugging-address=127.0.0.1", "--remote-debugging-port=0",
		"--no-first-run", "--no-default-browser-check", "--deny-permission-prompts", "--disable-notifications", "--disable-geolocation", "--window-size=900,900", "--app=about:blank")
	if e = command.Start(); e != nil {
		return fmt.Errorf("Не удалось открыть окно входа MAX")
	}
	defer func() { _ = command.Process.Kill(); _ = command.Wait() }()
	p.emit("progress", "Ожидание входа. Войдите в свой аккаунт MAX.", nil)
	endpoint := ""
	for i := 0; i < 300 && endpoint == ""; i++ {
		if b, e := os.ReadFile(filepath.Join(dir, "DevToolsActivePort")); e == nil {
			if lines := strings.Split(string(b), "\n"); len(lines) > 1 {
				endpoint = "http://127.0.0.1:" + lines[0]
				break
			}
		}
		time.Sleep(100 * time.Millisecond)
	}
	if endpoint == "" {
		return fmt.Errorf("Не удалось подключиться к окну MAX")
	}
	httpClient := &http.Client{Timeout: 20 * time.Second}
	tabs, e := browserTabs(httpClient, endpoint)
	if e != nil {
		return e
	}
	ws := ""
	for _, t := range tabs {
		if t.Type == "page" {
			ws = t.WebSocket
			break
		}
	}
	if ws == "" {
		return fmt.Errorf("Окно MAX не найдено")
	}
	conn, _, e := websocket.DefaultDialer.DialContext(ctx, ws, nil)
	if e != nil {
		return e
	}
	c := devTools{conn: conn, wait: 20 * time.Second}
	defer func() {
		c.wait = time.Second
		_, _ = c.call("Browser.close", map[string]any{})
		_ = c.conn.Close()
	}()
	if _, e = c.call("Page.navigate", map[string]any{"url": "https://web.max.ru"}); e != nil {
		return e
	}
	dark := r.Value == "dark"
	restored, targetOpened, tokenSeen := false, false, false
	for ctx.Err() == nil {
		time.Sleep(1500 * time.Millisecond)
		answer, e := c.evaluate(maxReadPage)
		if e != nil {
			if devToolsTimeout(e) {
				continue
			}
			p.emit("cancelled", "", nil)
			return nil
		}
		var page struct {
			URL   string `json:"url"`
			Token string `json:"token"`
			Auth  string `json:"auth"`
			Ready bool   `json:"ready"`
		}
		if json.Unmarshal([]byte(answer), &page) != nil || !maxURL(page.URL) {
			continue
		}
		_, _ = c.evaluate(maxThemeScript(dark))
		if !restored {
			restored = true
			if page.Auth == "" && previous.Auth != "" {
				text, _ := json.Marshal(previous.Auth)
				_, _ = c.evaluate("localStorage.setItem('__oneme_auth', " + string(text) + "); location.reload(); ''")
				continue
			}
		}
		if !page.Ready || !validMaxToken(page.Token) {
			continue
		}
		if !tokenSeen {
			tokenSeen = true
			if required != "" {
				p.emit("progress", "Токен получен. Открываем группу из ссылки подключения…", nil)
			} else {
				p.emit("progress", "Токен получен. Откройте группу для привязки.", nil)
			}
		}
		group := maxGroupFromURL(page.URL)
		if required != "" && group != required {
			if !targetOpened {
				targetOpened = true
				_, _ = c.call("Page.navigate", map[string]any{"url": "https://web.max.ru/" + required})
			}
			continue
		}
		if group == "" {
			continue
		}
		p.emit("progress", "Токен получен. Группа найдена — сохраняем привязку…", nil)
		encoded, _ := json.Marshal(maxAccount{Token: page.Token, Group: group, Auth: page.Auth})
		if e = secureSet("max", string(encoded)); e != nil {
			return fmt.Errorf("Не удалось сохранить привязку. Попробуйте ещё раз.")
		}
		p.emit("result", "", map[string]any{"linked": true, "group": group})
		return nil
	}
	if p.ctx.Err() != nil {
		p.emit("cancelled", "", nil)
		return nil
	}
	return fmt.Errorf("Не удалось дождаться входа в MAX. Попробуйте ещё раз.")
}
