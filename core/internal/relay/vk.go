package relay

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"github.com/gorilla/websocket"
	"github.com/zalando/go-keyring"
	"io"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"time"
)

const accountKey = "account"

type account struct {
	Token   string   `json:"token"`
	User    string   `json:"user"`
	Name    string   `json:"name"`
	Expires int64    `json:"expires"`
	Hashes  []string `json:"hashes"`
	// Call ids of the hashes this client created, so deleting a hash also ends its VK call.
	CallIDs    map[string]string `json:"callIds,omitempty"`
	Cookies    []browserCookie   `json:"cookies"`
	UserAgent  string            `json:"userAgent"`
	RetryAfter int64             `json:"retryAfter"`
}
type browserCookie struct {
	Name     string  `json:"name"`
	Value    string  `json:"value"`
	Domain   string  `json:"domain"`
	Path     string  `json:"path"`
	Secure   bool    `json:"secure"`
	HTTPOnly bool    `json:"httpOnly"`
	Expires  float64 `json:"expires"`
}

func readAccount() (account, error) {
	value, e := secureGet(accountKey)
	if e != nil {
		return account{}, e
	}
	var a account
	e = json.Unmarshal([]byte(value), &a)
	return a, e
}
func saveAccount(a account) error {
	b, e := json.Marshal(a)
	if e != nil {
		return e
	}
	return secureSet(accountKey, string(b))
}
func trustedVK(raw string) bool {
	u, e := url.Parse(raw)
	if e != nil || u.Scheme != "https" || u.User != nil || (u.Port() != "" && u.Port() != "443") {
		return false
	}
	host := strings.ToLower(u.Hostname())
	return host == "vk.com" || strings.HasSuffix(host, ".vk.com") || host == "vk.ru" || strings.HasSuffix(host, ".vk.ru")
}

var locationPattern = regexp.MustCompile(`location\.href\s*=\s*["']([^"']+)["']`)
var grantPattern = regexp.MustCompile(`https://login\.vk\.(?:com|ru)/\?act=grant_access[^"'\s<]+`)

func refreshToken(ctx context.Context, a *account) error {
	cookies := vkCookieHeader(a.Cookies)
	if cookies == "" {
		return fmt.Errorf("Сначала войдите в аккаунт VK")
	}
	bytes := make([]byte, 24)
	if _, e := rand.Read(bytes); e != nil {
		return e
	}
	state := hex.EncodeToString(bytes)
	client := &http.Client{Timeout: 20 * time.Second, CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}
	current := oauthURL(state)
	for hop := 0; hop < 12; hop++ {
		if !trustedVK(current) {
			break
		}
		request, e := http.NewRequestWithContext(ctx, http.MethodGet, current, nil)
		if e != nil {
			return e
		}
		request.Header.Set("User-Agent", a.UserAgent)
		request.Header.Set("Cookie", cookies)
		request.Header.Set("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8")
		response, e := client.Do(request)
		if e != nil {
			return fmt.Errorf("Подтвердите вход VK повторно")
		}
		body, e := io.ReadAll(io.LimitReader(response.Body, 2*1024*1024))
		response.Body.Close()
		if response.StatusCode == http.StatusTooManyRequests {
			return vkError{Code: 6}
		}
		if e != nil {
			return e
		}
		next := response.Header.Get("Location")
		if next == "" {
			if match := locationPattern.FindStringSubmatch(string(body)); len(match) == 2 {
				next = match[1]
			} else {
				next = grantPattern.FindString(string(body))
			}
		}
		next = strings.ReplaceAll(next, "&amp;", "&")
		base, _ := url.Parse(current)
		target, e := base.Parse(next)
		if e != nil || next == "" || !trustedVK(target.String()) {
			break
		}
		if updated, ok := oauthToken(target.String(), state); ok {
			if updated.User != "" {
				if a.User != "" && a.User != updated.User {
					a.Hashes = nil
				}
				a.User = updated.User
			}
			a.Token = updated.Token
			a.Expires = updated.Expires
			return nil
		}
		current = target.String()
	}
	return fmt.Errorf("Подтвердите вход VK повторно")
}
func accountInfo() (map[string]any, error) {
	a, e := readAccount()
	if e != nil {
		if errors.Is(e, keyring.ErrNotFound) {
			return map[string]any{"linked": false}, nil
		}
		return nil, e
	}
	return map[string]any{"linked": hasVKSession(a.Cookies), "name": a.Name, "user": a.User, "hashes": a.Hashes}, nil
}
func vkCookieHeader(cookies []browserCookie) string {
	seen := make(map[string]bool)
	var values []string
	for _, cookie := range cookies {
		if !trustedVK("https://"+strings.TrimPrefix(cookie.Domain, ".")+"/") || (cookie.Expires > 0 && cookie.Expires <= float64(time.Now().Unix())) {
			continue
		}
		value := cookie.Name + "=" + cookie.Value
		if !seen[value] {
			values = append(values, value)
			seen[value] = true
		}
	}
	return strings.Join(values, "; ")
}
func hasVKSession(cookies []browserCookie) bool {
	for _, cookie := range cookies {
		if cookie.Name == "remixsid" && cookie.Value != "" && trustedVK("https://"+strings.TrimPrefix(cookie.Domain, ".")+"/") && (cookie.Expires <= 0 || cookie.Expires > float64(time.Now().Unix())) {
			return true
		}
	}
	return false
}
func vkLoginComplete(location string, cookies []browserCookie) bool {
	if !trustedVK(location) {
		return false
	}
	address, _ := url.Parse(location)
	path := strings.ToLower(address.Path)
	if path == "/login" || path == "/login.php" || path == "/auth" || path == "/authorize" || address.Query().Get("act") == "login" {
		return false
	}
	text := strings.ToLower(location)
	if (address.Hostname() == "id.vk.com" || address.Hostname() == "id.vk.ru") && (strings.Contains(text, "authorize") || strings.Contains(text, "login") || strings.Contains(text, "auth")) {
		return false
	}
	for _, cookie := range cookies {
		if cookie.Name == "remixsid" && len(cookie.Value) >= 8 && hasVKSession([]browserCookie{cookie}) {
			return true
		}
	}
	return false
}
func sameVKAccount(a, b account) bool {
	if a.User != "" && b.User != "" {
		return a.User == b.User
	}
	for _, first := range a.Cookies {
		if first.Name != "remixsid" || first.Value == "" {
			continue
		}
		for _, second := range b.Cookies {
			if second.Name == "remixsid" && first.Value == second.Value {
				return true
			}
		}
	}
	return false
}

var vkCookieURLs = []string{"https://vk.com", "https://vk.ru", "https://m.vk.com", "https://m.vk.ru", "https://login.vk.com", "https://login.vk.ru", "https://oauth.vk.com", "https://oauth.vk.ru", "https://id.vk.com", "https://id.vk.ru"}

// The login window signs in on m.vk.ru, and oauth.vk.com would not see that session and ask to sign in again.
func oauthURL(state string) string {
	return "https://oauth.vk.ru/authorize?" + url.Values{"client_id": {"6287487"}, "display": {"mobile"}, "redirect_uri": {"https://oauth.vk.ru/blank.html"}, "response_type": {"token"}, "scope": {"messages"}, "v": {"5.199"}, "state": {state}}.Encode()
}
func oauthToken(raw, state string) (account, bool) {
	u, e := url.Parse(raw)
	if e != nil || u.Scheme != "https" || u.User != nil || (u.Port() != "" && u.Port() != "443") || (u.Hostname() != "oauth.vk.com" && u.Hostname() != "oauth.vk.ru") || u.Path != "/blank.html" {
		return account{}, false
	}
	q, e := url.ParseQuery(u.Fragment)
	if e != nil || q.Get("state") != state || q.Get("access_token") == "" {
		return account{}, false
	}
	expiry := time.Now().Add(365 * 24 * time.Hour).Unix()
	if seconds, e := time.ParseDuration(q.Get("expires_in") + "s"); e == nil && seconds > 0 {
		expiry = time.Now().Add(seconds).Unix()
	}
	return account{Token: q.Get("access_token"), User: q.Get("user_id"), Expires: expiry}, true
}

type devTools struct {
	conn *websocket.Conn
	id   int
}
type browserTab struct {
	Type      string `json:"type"`
	URL       string `json:"url"`
	WebSocket string `json:"webSocketDebuggerUrl"`
}

func browserTabs(client *http.Client, endpoint string) ([]browserTab, error) {
	response, err := client.Get(endpoint + "/json/list")
	if err != nil {
		return nil, err
	}
	defer response.Body.Close()
	var tabs []browserTab
	err = json.NewDecoder(io.LimitReader(response.Body, 1024*1024)).Decode(&tabs)
	return tabs, err
}

func (c *devTools) call(method string, params any) (json.RawMessage, error) {
	c.id++
	id := c.id
	if e := c.conn.WriteJSON(map[string]any{"id": id, "method": method, "params": params}); e != nil {
		return nil, e
	}
	for {
		var response struct {
			ID     int             `json:"id"`
			Result json.RawMessage `json:"result"`
			Error  *struct {
				Message string `json:"message"`
			} `json:"error"`
		}
		if e := c.conn.ReadJSON(&response); e != nil {
			return nil, e
		}
		if response.ID == id {
			if response.Error != nil {
				return nil, fmt.Errorf("Ошибка окна VK")
			}
			return response.Result, nil
		}
	}
}
func (c *devTools) evaluate(expression string) (string, error) {
	result, e := c.call("Runtime.evaluate", map[string]any{"expression": expression, "returnByValue": true})
	if e != nil {
		return "", e
	}
	var r struct {
		Result struct {
			Value string `json:"value"`
		} `json:"result"`
	}
	e = json.Unmarshal(result, &r)
	return r.Result.Value, e
}
func browserPath() string {
	names := []string{"msedge", "google-chrome", "chromium", "chromium-browser", "chrome"}
	for _, name := range names {
		if path, e := exec.LookPath(name); e == nil {
			return path
		}
	}
	paths := []string{}
	if runtime.GOOS == "windows" {
		for _, root := range []string{os.Getenv("PROGRAMFILES"), os.Getenv("PROGRAMFILES(X86)"), os.Getenv("LOCALAPPDATA")} {
			for _, tail := range []string{"Microsoft/Edge/Application/msedge.exe", "Google/Chrome/Application/chrome.exe"} {
				paths = append(paths, filepath.Join(root, filepath.FromSlash(tail)))
			}
		}
	}
	if runtime.GOOS == "darwin" {
		paths = append(paths, "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge", "/Applications/Chromium.app/Contents/MacOS/Chromium")
	}
	for _, path := range paths {
		if stat, e := os.Stat(path); e == nil && !stat.IsDir() {
			return path
		}
	}
	return ""
}
func (p *protocol) login(r Request) error {
	tokenMode := r.Operation == "vk-token"
	previous, _ := readAccount()
	path := browserPath()
	if path == "" {
		return fmt.Errorf("Для привязки VK установите Chrome, Chromium или Edge")
	}
	// A private, temporary profile avoids reading the user's existing browser sessions.
	dir, e := os.MkdirTemp("", "qthrone-vk-")
	if e != nil {
		return e
	}
	defer os.RemoveAll(dir)
	ctx, cancel := context.WithTimeout(p.ctx, 10*time.Minute)
	defer cancel()
	stateBytes := make([]byte, 24)
	if _, e = rand.Read(stateBytes); e != nil {
		return e
	}
	state := hex.EncodeToString(stateBytes)
	command := exec.CommandContext(ctx, path, "--user-data-dir="+dir, "--remote-debugging-address=127.0.0.1", "--remote-debugging-port=0", "--window-size=480,680", "--no-first-run", "--no-default-browser-check", "--disable-popup-blocking", "--app=about:blank")
	if e = command.Start(); e != nil {
		return fmt.Errorf("Не удалось открыть окно входа VK")
	}
	defer func() { _ = command.Process.Kill(); _ = command.Wait() }()
	p.emit("progress", "Войдите в VK в открывшемся окне", nil)
	var endpoint string
	for i := 0; i < 100; i++ {
		if b, e := os.ReadFile(filepath.Join(dir, "DevToolsActivePort")); e == nil {
			lines := strings.Split(string(b), "\n")
			if len(lines) > 1 {
				endpoint = "http://127.0.0.1:" + lines[0]
				break
			}
		}
		time.Sleep(100 * time.Millisecond)
	}
	if endpoint == "" {
		return fmt.Errorf("Не удалось подключиться к окну VK")
	}
	httpClient := &http.Client{Timeout: 5 * time.Second}
	tabs, e := browserTabs(httpClient, endpoint)
	if e != nil {
		return e
	}
	var ws string
	for _, t := range tabs {
		if t.Type == "page" {
			ws = t.WebSocket
			break
		}
	}
	if ws == "" {
		return fmt.Errorf("Окно VK не найдено")
	}
	conn, _, e := websocket.DefaultDialer.DialContext(ctx, ws, nil)
	if e != nil {
		return e
	}
	defer conn.Close()
	c := devTools{conn: conn}
	defer func() { _ = c.conn.Close() }()
	_ = conn.SetReadDeadline(time.Now().Add(5 * time.Second))
	_, _ = c.call("Network.enable", map[string]any{})
	_, _ = c.call("Network.setExtraHTTPHeaders", map[string]any{"headers": map[string]string{"Accept-Language": "ru-RU,ru;q=0.9,en-US;q=0.8,en;q=0.7"}})
	var restored []map[string]any
	for _, cookie := range previous.Cookies {
		if !trustedVK("https://"+strings.TrimPrefix(cookie.Domain, ".")+"/") || (cookie.Expires > 0 && cookie.Expires <= float64(time.Now().Unix())) {
			continue
		}
		value := map[string]any{"name": cookie.Name, "value": cookie.Value, "domain": cookie.Domain, "path": cookie.Path, "secure": cookie.Secure, "httpOnly": cookie.HTTPOnly}
		if cookie.Expires > 0 {
			value["expires"] = cookie.Expires
		}
		restored = append(restored, value)
	}
	if len(restored) > 0 {
		if _, e = c.call("Network.setCookies", map[string]any{"cookies": restored}); e != nil {
			return fmt.Errorf("Не удалось восстановить сессию VK")
		}
	}
	startURL := "https://m.vk.ru/login"
	if tokenMode {
		startURL = oauthURL(state)
	}
	if _, e = c.call("Page.navigate", map[string]any{"url": startURL}); e != nil {
		return e
	}
	defer func() {
		_ = c.conn.SetReadDeadline(time.Now().Add(time.Second))
		_, _ = c.call("Browser.close", map[string]any{})
	}()
	var saved account
	linked := false
	loginRoute := 0
	for ctx.Err() == nil {
		if tokenMode {
			pages, err := browserTabs(httpClient, endpoint)
			if err == nil {
				for _, page := range pages {
					if a, ok := oauthToken(page.URL, state); ok {
						saved = a
						linked = true
						break
					}
				}
			}
			if linked {
				break
			}
		}
		_ = c.conn.SetReadDeadline(time.Now().Add(5 * time.Second))
		location, e := c.evaluate("location.href")
		if e != nil {
			pages, _ := browserTabs(httpClient, endpoint)
			for _, page := range pages {
				if page.Type != "page" || page.WebSocket == ws || !trustedVK(page.URL) {
					continue
				}
				next, _, err := websocket.DefaultDialer.DialContext(ctx, page.WebSocket, nil)
				if err == nil {
					_ = c.conn.Close()
					c.conn = next
					ws = page.WebSocket
					break
				}
			}
			if c.conn != conn {
				conn = c.conn
				continue
			}
			p.emit("cancelled", "", nil)
			if tokenMode {
				return context.Canceled
			}
			return nil
		}
		if a, ok := oauthToken(location, state); ok && tokenMode {
			saved = a
			linked = true
			break
		}
		if !tokenMode {
			body, _ := c.evaluate("document.body ? document.body.innerText : ''")
			if strings.Contains(strings.ToLower(body), "unknown method") && loginRoute < 2 {
				loginRoute++
				next := "https://m.vk.ru/"
				if loginRoute == 2 {
					next = "https://vk.ru/login"
					_, _ = c.call("Network.setUserAgentOverride", map[string]any{"userAgent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"})
				}
				_, _ = c.call("Network.clearBrowserCookies", map[string]any{})
				_, _ = c.call("Page.navigate", map[string]any{"url": next})
			}
			result, e := c.call("Network.getCookies", map[string]any{"urls": vkCookieURLs})
			if e != nil {
				return e
			}
			var cookies struct {
				Cookies []browserCookie `json:"cookies"`
			}
			_ = json.Unmarshal(result, &cookies)
			ready, _ := c.evaluate("document.readyState")
			if ready == "complete" && vkLoginComplete(location, cookies.Cookies) {
				saved.Cookies = cookies.Cookies
				linked = true
				break
			}
		}
		select {
		case <-ctx.Done():
		case <-time.After(500 * time.Millisecond):
		}
	}
	if !linked {
		return fmt.Errorf("Истекло время входа VK")
	}
	result, e := c.call("Network.getCookies", map[string]any{"urls": vkCookieURLs})
	if e != nil {
		return e
	}
	var cookies struct {
		Cookies []browserCookie `json:"cookies"`
	}
	if e = json.Unmarshal(result, &cookies); e != nil {
		return e
	}
	saved.Cookies = cookies.Cookies
	saved.UserAgent, _ = c.evaluate("navigator.userAgent")
	if !tokenMode {
		// Take the calls token in this window too, so creating hashes later needs no second window.
		p.emit("progress", "Подтвердите доступ VK в том же окне", nil)
		if _, e = c.call("Page.navigate", map[string]any{"url": oauthURL(state)}); e == nil {
			deadline := time.Now().Add(2 * time.Minute)
			for ctx.Err() == nil && time.Now().Before(deadline) && saved.Token == "" {
				pages, err := browserTabs(httpClient, endpoint)
				if err != nil || len(pages) == 0 {
					break // the window was closed; the session alone is still linked
				}
				for _, page := range pages {
					if a, ok := oauthToken(page.URL, state); ok {
						saved.Token, saved.Expires, saved.User = a.Token, a.Expires, a.User
					}
				}
				time.Sleep(500 * time.Millisecond)
			}
		}
	}
	if saved.User == "" {
		for _, cookie := range saved.Cookies {
			if cookie.Name == "remixmid" && cookie.Value != "0" {
				saved.User = cookie.Value
				break
			}
		}
	}
	if saved.Name == "" {
		saved.Name = "Аккаунт VK привязан"
		if saved.User != "" {
			saved.Name = "VK ID: " + saved.User
		}
	}
	if !tokenMode {
		unlock, e := vaultLock(p.ctx, "account")
		if e != nil {
			return e
		}
		defer unlock()
	}
	if old, e := readAccount(); e == nil && sameVKAccount(old, saved) {
		saved.Hashes = old.Hashes
		if !tokenMode && saved.Token == "" {
			saved.Token = old.Token
			saved.Expires = old.Expires
		}
		saved.RetryAfter = old.RetryAfter
	}
	if e = saveAccount(saved); e != nil {
		return fmt.Errorf("Не удалось сохранить аккаунт в системном хранилище: %w", e)
	}
	if !tokenMode {
		p.emit("result", "", map[string]any{"linked": true, "name": saved.Name, "user": saved.User})
	}
	return nil
}

type vkError struct{ Code int }

func (e vkError) Error() string { return fmt.Sprintf("VK: код %d", e.Code) }
func vkAPI(ctx context.Context, token, method string, params ...string) (json.RawMessage, error) {
	client := &http.Client{Timeout: 20 * time.Second}
	query := url.Values{"access_token": {token}, "v": {"5.199"}}
	for i := 0; i+1 < len(params); i += 2 {
		query.Set(params[i], params[i+1])
	}
	request, e := http.NewRequestWithContext(ctx, http.MethodGet, "https://api.vk.ru/method/"+method+"?"+query.Encode(), nil)
	if e != nil {
		return nil, e
	}
	request.Header.Set("User-Agent", "okhttp/4.12.0")
	response, e := client.Do(request)
	if e != nil {
		return nil, fmt.Errorf("VK недоступен")
	}
	defer response.Body.Close()
	if response.StatusCode == http.StatusTooManyRequests {
		return nil, vkError{Code: 6}
	}
	if response.StatusCode != 200 {
		return nil, fmt.Errorf("VK: HTTP %d", response.StatusCode)
	}
	var data struct {
		Response json.RawMessage `json:"response"`
		Error    *struct {
			Code int `json:"error_code"`
		} `json:"error"`
	}
	if e = json.NewDecoder(io.LimitReader(response.Body, 1024*1024)).Decode(&data); e != nil {
		return nil, fmt.Errorf("Некорректный ответ VK")
	}
	if data.Error != nil {
		return nil, vkError{Code: data.Error.Code}
	}
	if len(data.Response) == 0 {
		return nil, fmt.Errorf("VK не вернул результат")
	}
	return data.Response, nil
}
func (p *protocol) confirmVKBrowser() error {
	p.emit("vk-confirm", "VK не выдал токен через HTTP. Открыть окно VK для подтверждения доступа?", nil)
	select {
	case answer, ok := <-p.commands:
		if ok && answer == "approve" {
			return nil
		}
		return fmt.Errorf("Подтверждение доступа VK отменено")
	case <-p.ctx.Done():
		return p.ctx.Err()
	case <-time.After(2 * time.Minute):
		return fmt.Errorf("Подтверждение доступа VK не получено")
	}
}

func (p *protocol) hashes(r Request) error {
	if r.Count < 1 || r.Count > maxAccountHashes {
		return fmt.Errorf("Количество хешей: от 1 до %d", maxAccountHashes)
	}
	unlock, e := vaultLock(p.ctx, "account")
	if e != nil {
		return e
	}
	defer unlock()
	a, e := readAccount()
	if e != nil {
		if errors.Is(e, keyring.ErrNotFound) {
			return fmt.Errorf("Сначала привяжите аккаунт VK")
		}
		return e
	}
	if len(a.Hashes) >= r.Count {
		p.emit("result", "", map[string]any{"hashes": a.Hashes[:r.Count]})
		return nil
	}
	if !hasVKSession(a.Cookies) {
		return fmt.Errorf("Сначала войдите в аккаунт VK")
	}
	if a.RetryAfter > time.Now().Unix() {
		return fmt.Errorf("VK ограничил запросы. Повторите позже или используйте ручные хеши")
	}
	if e = p.tokenForCalls(&a); e != nil {
		return e
	}
	created := 0
	refreshed := false
	for len(a.Hashes) < r.Count {
		if created > 0 {
			select {
			case <-p.ctx.Done():
				return p.ctx.Err()
			case <-time.After(2 * time.Second):
			}
		}
		result, e := vkAPI(p.ctx, a.Token, "calls.start")
		if e != nil {
			var apiErr vkError
			if errors.As(e, &apiErr) {
				switch apiErr.Code {
				case 4, 5, 27, 28:
					a.Expires = 0
				case 6, 9, 14, 29:
					a.RetryAfter = time.Now().Add(time.Hour).Unix()
				}
				_ = saveAccount(a)
				if (apiErr.Code == 4 || apiErr.Code == 5 || apiErr.Code == 27 || apiErr.Code == 28) && !refreshed {
					refreshed = true
					if e = p.tokenForCalls(&a); e != nil {
						return e
					}
					continue
				}
			}
			return e
		}
		var call struct {
			Link string `json:"join_link"`
			ID   string `json:"call_id"`
		}
		if json.Unmarshal(result, &call) != nil {
			return fmt.Errorf("VK не вернул ссылку звонка")
		}
		u, e := url.Parse(call.Link)
		if e != nil || !trustedVK(call.Link) {
			return fmt.Errorf("Некорректная ссылка звонка")
		}
		hash := strings.TrimPrefix(u.Path, "/call/join/")
		if len(hash) < 16 || strings.ContainsAny(hash, "/|, \r\n") {
			return fmt.Errorf("Некорректный хеш VK")
		}
		a.Hashes = append(a.Hashes, hash)
		if call.ID != "" {
			if a.CallIDs == nil {
				a.CallIDs = map[string]string{}
			}
			a.CallIDs[hash] = call.ID
		}
		created++
		if e = saveAccount(a); e != nil {
			return e
		}
		p.emit("progress", fmt.Sprintf("Создано хешей: %d/%d", len(a.Hashes), r.Count), nil)
	}
	p.emit("result", "", a.Hashes[:r.Count])
	return nil
}

const maxAccountHashes = 30

// Removes hashes from the account and ends the VK calls behind them. A call VK refuses to end, or one
// created before call ids were kept, is still removed here; the result counts those left running.
func (p *protocol) deleteHashes(r Request) error {
	unlock, e := vaultLock(p.ctx, "account")
	if e != nil {
		return e
	}
	defer unlock()
	a, e := readAccount()
	if e != nil {
		return fmt.Errorf("Сначала привяжите аккаунт VK")
	}
	drop := map[string]bool{}
	for _, hash := range r.Hashes {
		drop[hash] = true
	}
	unfinished := 0
	tokenReady := false
	var kept []string
	for _, hash := range a.Hashes {
		if !drop[hash] {
			kept = append(kept, hash)
			continue
		}
		id := a.CallIDs[hash]
		delete(a.CallIDs, hash)
		if id == "" {
			unfinished++
			continue
		}
		if !tokenReady {
			tokenReady = p.tokenForCalls(&a) == nil
		}
		if !tokenReady {
			unfinished++
			continue
		}
		if _, err := vkAPI(p.ctx, a.Token, "calls.forceFinish", "call_id", id); err != nil {
			unfinished++
		}
	}
	a.Hashes = kept
	if e = saveAccount(a); e != nil {
		return e
	}
	p.emit("result", "", map[string]any{"hashes": a.Hashes, "unfinished": unfinished})
	return nil
}

// The account binding stays qWDTT's cookie flow for every relay client.
func obtainVKToken(a *account, now int64, refresh, browser func() error) error {
	if a.Token != "" && a.Expires > now+120 {
		return nil
	}
	err := refresh()
	if err != nil {
		var limit vkError
		if errors.As(err, &limit) && limit.Code == 6 {
			return err
		}
		if err = browser(); err != nil {
			return err
		}
	}
	if a.Token == "" {
		return fmt.Errorf("Не удалось получить доступ VK. Подтвердите вход повторно")
	}
	return nil
}

func (p *protocol) tokenForCalls(a *account) error {
	err := obtainVKToken(a, time.Now().Unix(), func() error { return refreshToken(p.ctx, a) }, func() error {
		if p.vkBrowserUsed {
			return fmt.Errorf("VK отклонил доступ после подтверждения. Повторное окно входа не открыто; проверьте доступ аккаунта или используйте ручные хеши")
		}
		p.vkBrowserUsed = true
		if err := p.confirmVKBrowser(); err != nil {
			return err
		}
		if err := p.login(Request{Operation: "vk-token"}); err != nil {
			return err
		}
		latest, err := readAccount()
		if err == nil {
			*a = latest
		}
		return err
	})
	var limit vkError
	if errors.As(err, &limit) && limit.Code == 6 {
		a.RetryAfter = time.Now().Add(time.Hour).Unix()
	}
	if saved := saveAccount(*a); err == nil {
		err = saved
	}
	return err
}

func (p *protocol) callsToken() error {
	unlock, err := vaultLock(p.ctx, "account")
	if err != nil {
		return err
	}
	defer unlock()
	a, err := readAccount()
	if err != nil || !hasVKSession(a.Cookies) {
		return fmt.Errorf("Сначала привяжите аккаунт VK")
	}
	if a.RetryAfter > time.Now().Unix() {
		return fmt.Errorf("VK ограничил запросы. Повторите позже")
	}
	if err = p.tokenForCalls(&a); err != nil {
		return err
	}
	p.emit("result", "", a.Token)
	return nil
}
