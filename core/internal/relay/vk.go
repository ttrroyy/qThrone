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
	"net/http/cookiejar"
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
	Token      string          `json:"token"`
	User       string          `json:"user"`
	Name       string          `json:"name"`
	Expires    int64           `json:"expires"`
	Hashes     []string        `json:"hashes"`
	Cookies    []browserCookie `json:"cookies"`
	UserAgent  string          `json:"userAgent"`
	RetryAfter int64           `json:"retryAfter"`
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
var grantPattern = regexp.MustCompile(`https://login\.vk\.com/\?act=grant_access[^"'\s<]+`)

func refreshToken(ctx context.Context, a *account) error {
	jar, _ := cookiejar.New(nil)
	for _, c := range a.Cookies {
		domain := strings.TrimPrefix(c.Domain, ".")
		if !trustedVK("https://" + domain + "/") {
			continue
		}
		u, _ := url.Parse("https://" + domain + "/")
		cookie := &http.Cookie{Name: c.Name, Value: c.Value, Path: c.Path, Domain: c.Domain, Secure: c.Secure, HttpOnly: c.HTTPOnly}
		if c.Expires > 0 {
			cookie.Expires = time.Unix(int64(c.Expires), 0)
		}
		jar.SetCookies(u, []*http.Cookie{cookie})
	}
	bytes := make([]byte, 24)
	if _, e := rand.Read(bytes); e != nil {
		return e
	}
	state := hex.EncodeToString(bytes)
	client := &http.Client{Timeout: 30 * time.Second, Jar: jar, CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}
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
		response, e := client.Do(request)
		if e != nil {
			return fmt.Errorf("Подтвердите вход VK повторно")
		}
		body, e := io.ReadAll(io.LimitReader(response.Body, 2*1024*1024))
		response.Body.Close()
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
			a.Token = updated.Token
			a.Expires = updated.Expires
			return saveAccount(*a)
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
	return map[string]any{"linked": a.Token != "", "name": a.Name, "user": a.User, "hashes": a.Hashes}, nil
}
func oauthURL(state string) string {
	return "https://oauth.vk.com/authorize?" + url.Values{"client_id": {"6287487"}, "display": {"mobile"}, "redirect_uri": {"https://oauth.vk.com/blank.html"}, "response_type": {"token"}, "scope": {"messages"}, "v": {"5.199"}, "state": {state}}.Encode()
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
	command := exec.CommandContext(ctx, path, "--user-data-dir="+dir, "--remote-debugging-address=127.0.0.1", "--remote-debugging-port=0", "--no-first-run", "--no-default-browser-check", "--app=https://m.vk.ru/login")
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
	var tabs []struct {
		Type      string `json:"type"`
		URL       string `json:"url"`
		WebSocket string `json:"webSocketDebuggerUrl"`
	}
	response, e := httpClient.Get(endpoint + "/json/list")
	if e != nil {
		return e
	}
	e = json.NewDecoder(response.Body).Decode(&tabs)
	response.Body.Close()
	if e != nil {
		return e
	}
	var ws string
	for _, t := range tabs {
		if t.Type == "page" && strings.HasPrefix(t.URL, "https://m.vk.ru/") {
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
	defer func() {
		_ = conn.SetReadDeadline(time.Now().Add(time.Second))
		_, _ = c.call("Browser.close", map[string]any{})
	}()
	var saved account
	linked := false
	authorizationStarted := false
	loginRoute := 0
	for ctx.Err() == nil {
		_ = conn.SetReadDeadline(time.Now().Add(5 * time.Second))
		location, e := c.evaluate("location.href")
		if e != nil {
			return fmt.Errorf("Окно входа VK закрыто")
		}
		if a, ok := oauthToken(location, state); ok {
			saved = a
			linked = true
			break
		}
		if !authorizationStarted {
			body, _ := c.evaluate("document.body ? document.body.innerText : ''")
			if strings.Contains(strings.ToLower(body), "unknown method") && loginRoute < 2 {
				loginRoute++
				next := "https://m.vk.ru/"
				if loginRoute == 2 {
					next = "https://vk.ru/login"
					_, _ = c.call("Network.setUserAgentOverride", map[string]any{"userAgent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"})
				}
				_, _ = c.call("Page.navigate", map[string]any{"url": next})
			}
			result, e := c.call("Network.getCookies", map[string]any{"urls": []string{"https://vk.ru/", "https://vk.com/", "https://m.vk.ru/"}})
			if e != nil {
				return e
			}
			var cookies struct {
				Cookies []browserCookie `json:"cookies"`
			}
			_ = json.Unmarshal(result, &cookies)
			for _, cookie := range cookies.Cookies {
				if cookie.Name == "remixsid" && cookie.Value != "" {
					authorizationStarted = true
					_, e = c.call("Page.navigate", map[string]any{"url": oauthURL(state)})
					if e != nil {
						return e
					}
					p.emit("progress", "Подтвердите доступ VK для создания хешей", nil)
					break
				}
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
	result, e := c.call("Network.getCookies", map[string]any{"urls": []string{"https://vk.ru/", "https://vk.com/", "https://m.vk.ru/", "https://m.vk.com/", "https://oauth.vk.com/", "https://login.vk.com/"}})
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
	identity, e := vkAPI(p.ctx, saved.Token, "users.get")
	if e == nil {
		var users []struct {
			First string `json:"first_name"`
			Last  string `json:"last_name"`
		}
		if json.Unmarshal(identity, &users) == nil && len(users) > 0 {
			saved.Name = strings.TrimSpace(users[0].First + " " + users[0].Last)
		}
	}
	if saved.Name == "" {
		saved.Name = "VK ID: " + saved.User
	}
	unlock, e := vaultLock(p.ctx, "account")
	if e != nil {
		return e
	}
	defer unlock()
	if old, e := readAccount(); e == nil && old.User == saved.User {
		saved.Hashes = old.Hashes
	}
	if e = saveAccount(saved); e != nil {
		return fmt.Errorf("Не удалось сохранить аккаунт в системном хранилище: %w", e)
	}
	p.emit("result", "", map[string]any{"linked": true, "name": saved.Name, "user": saved.User})
	return nil
}

type vkError struct{ Code int }

func (e vkError) Error() string { return fmt.Sprintf("VK: код %d", e.Code) }
func vkAPI(ctx context.Context, token, method string) (json.RawMessage, error) {
	client := &http.Client{Timeout: 30 * time.Second}
	request, e := http.NewRequestWithContext(ctx, http.MethodPost, "https://api.vk.ru/method/"+method, strings.NewReader(url.Values{"access_token": {token}, "v": {"5.199"}}.Encode()))
	if e != nil {
		return nil, e
	}
	request.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	response, e := client.Do(request)
	if e != nil {
		return nil, fmt.Errorf("VK недоступен")
	}
	defer response.Body.Close()
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
func (p *protocol) hashes(r Request) error {
	if r.Count < 1 || r.Count > 4 {
		return fmt.Errorf("Количество хешей: от 1 до 4")
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
	if a.Expires <= time.Now().Unix()+60 {
		if e = refreshToken(p.ctx, &a); e != nil {
			return e
		}
	}
	if a.RetryAfter > time.Now().Unix() {
		return fmt.Errorf("VK ограничил запросы. Повторите позже или используйте ручные хеши")
	}
	for len(a.Hashes) < r.Count {
		if len(a.Hashes) > 0 {
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
			}
			return e
		}
		var call struct {
			Link string `json:"join_link"`
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
		if e = saveAccount(a); e != nil {
			return e
		}
		p.emit("progress", fmt.Sprintf("Создано хешей: %d/%d", len(a.Hashes), r.Count), nil)
	}
	p.emit("result", "", a.Hashes[:r.Count])
	return nil
}
