//go:build !android

package main

import (
	"context"
	"errors"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"time"

	"github.com/chromedp/cdproto/network"
	"github.com/chromedp/chromedp"
)

// deliverCaptcha: desktops answer in their own window, not over stdin.
func deliverCaptcha(string) {}

// captchaInteractive: the connection itself may show a window; tests (interactive_captcha false)
// may not, like qWDTT's captcha.
func captchaInteractive(configured bool) bool { return configured }

func edgeExecutable() string {
	for _, name := range []string{"msedge", "microsoft-edge", "microsoft-edge-stable"} {
		if path, err := exec.LookPath(name); err == nil {
			return path
		}
	}
	var candidates []string
	if runtime.GOOS == "windows" {
		for _, key := range []string{"ProgramFiles(x86)", "ProgramFiles", "LOCALAPPDATA"} {
			if root := os.Getenv(key); root != "" {
				candidates = append(candidates, filepath.Join(root, "Microsoft", "Edge", "Application", "msedge.exe"))
			}
		}
	} else if runtime.GOOS == "darwin" {
		candidates = append(candidates, "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge")
	}
	for _, path := range candidates {
		if info, err := os.Stat(path); err == nil && !info.IsDir() {
			return path
		}
	}
	return ""
}

// The fork app's check (openflux-app CaptchaWebViewDialog): its browser, the hosts it reads cookies
// from, its cookie-banner click and its ready test on the Yandex document itself.
const forkUserAgent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"

var forkCookieHosts = []string{"https://yandex.com/", "https://yandex.ru/", "https://passport.yandex.ru/", "https://auth.yandex.ru/",
	"https://disk.yandex.ru/", "https://docs.yandex.ru/"}

const forkAcceptConsent = `(function () {
  var wanted = ['Allow all', 'Разрешить все', 'Accept all', 'Allow essential cookies', 'Разрешить обязательные'];
  var nodes = document.querySelectorAll('button, [role=button], a');
  for (var i = 0; i < nodes.length; i++) {
    var t = (nodes[i].innerText || nodes[i].textContent || '').trim();
    for (var j = 0; j < wanted.length; j++) { if (t === wanted[j]) { nodes[i].click(); return 'clicked'; } }
  }
  return 'none';
})()`

const forkReady = `(function () {
  if (location.pathname.toLowerCase().indexOf('captcha') !== -1) return false;
  return document.readyState === 'complete' && !!document.getElementById('client-config');
})()`

// solveCaptcha opens the page in a private browser window (its own temporary profile, so the
// user's browser, tabs and cookies stay untouched) and returns the cookies once the Yandex document
// itself has loaded, as the fork's app does. Closing the window declines the check for now.
func solveCaptcha(page string) (string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Minute)
	defer cancel()
	profile, err := os.MkdirTemp("", "qthrone-openflux-check-")
	if err != nil {
		return "", errors.New("cannot create a private browser profile")
	}
	defer os.RemoveAll(profile)
	proxy, closeProxy, err := startCaptchaProxy(ctx)
	if err != nil {
		return "", errors.New("cannot start the private check connection")
	}
	defer closeProxy()
	opts := append([]chromedp.ExecAllocatorOption{}, chromedp.DefaultExecAllocatorOptions[:]...)
	if edge := edgeExecutable(); edge != "" {
		opts = append(opts, chromedp.ExecPath(edge))
	}
	// No --app: with the allocator's initial about:blank tab Chromium may open a second window
	// and navigate the hidden one.
	opts = append(opts, chromedp.Flag("headless", false), chromedp.UserDataDir(profile), chromedp.WindowSize(560, 760), chromedp.UserAgent(forkUserAgent),
		chromedp.ProxyServer(proxy), chromedp.Flag("remote-debugging-address", "127.0.0.1"))
	alloc, closeAlloc := chromedp.NewExecAllocator(ctx, opts...)
	defer closeAlloc()
	tab, closeTab := chromedp.NewContext(alloc)
	defer closeTab()
	if err := chromedp.Run(tab, network.Enable(), chromedp.Navigate(page)); err != nil {
		if ctx.Err() != nil {
			return "", ctx.Err()
		}
		return "", errors.New("cannot open the check in the private browser")
	}
	for {
		select {
		case <-ctx.Done():
			return "", ctx.Err()
		case <-time.After(700 * time.Millisecond):
		}
		var clicked string
		var ready bool
		if err := chromedp.Run(tab, chromedp.Evaluate(forkAcceptConsent, &clicked), chromedp.Evaluate(forkReady, &ready)); err != nil {
			// The window was closed.
			return "", errors.New("the check window was closed")
		}
		if !ready {
			continue
		}
		var current string
		_ = chromedp.Run(tab, chromedp.Location(&current))
		var cookies []*network.Cookie
		if err := chromedp.Run(tab, chromedp.ActionFunc(func(c context.Context) error {
			var e error
			cookies, e = network.GetCookies().WithURLs(append([]string{page, current}, forkCookieHosts...)).Do(c)
			return e
		})); err != nil {
			return "", errors.New("cannot read the check's cookies")
		}
		merged := map[string]string{}
		var order []string
		for _, cookie := range cookies {
			if _, seen := merged[cookie.Name]; !seen {
				order = append(order, cookie.Name)
			}
			merged[cookie.Name] = cookie.Value
		}
		parts := make([]string, 0, len(order))
		for _, name := range order {
			parts = append(parts, name+"="+merged[name])
		}
		return strings.Join(parts, "; "), nil
	}
}

// As qWDTT's captcha (qwdtt/captcha_proxy.go): only the private check browser uses this short-lived
// HTTPS proxy. Its connections belong to this helper, which qThrone already excludes from its TUN, so
// the check passes on the same address the transport uses.
// No global Edge/Chrome routing exemption or system proxy change is needed.
func startCaptchaProxy(parent context.Context) (string, func(), error) {
	return startCaptchaProxyWithDial(parent, (&net.Dialer{Timeout: 15 * time.Second}).DialContext)
}

func startCaptchaProxyWithDial(parent context.Context, dial func(context.Context, string, string) (net.Conn, error)) (string, func(), error) {
	ctx, cancel := context.WithCancel(parent)
	ln, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		cancel()
		return "", nil, err
	}
	var mu sync.Mutex
	connections := make(map[net.Conn]bool)
	track := func(c net.Conn) bool {
		mu.Lock()
		defer mu.Unlock()
		if ctx.Err() != nil {
			c.Close()
			return false
		}
		connections[c] = true
		return true
	}
	forget := func(c net.Conn) {
		c.Close()
		mu.Lock()
		delete(connections, c)
		mu.Unlock()
	}
	server := &http.Server{ReadHeaderTimeout: 10 * time.Second}
	server.Handler = http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, port, err := net.SplitHostPort(r.Host)
		if r.Method != http.MethodConnect || err != nil || port != "443" {
			http.Error(w, "HTTPS CONNECT required", http.StatusForbidden)
			return
		}
		remote, err := dial(ctx, "tcp4", r.Host)
		if err != nil {
			http.Error(w, "connection failed", http.StatusBadGateway)
			return
		}
		if !track(remote) {
			return
		}
		defer forget(remote)
		local, buffered, err := w.(http.Hijacker).Hijack()
		if err != nil {
			return
		}
		if !track(local) {
			return
		}
		defer forget(local)
		if _, err = buffered.WriteString("HTTP/1.1 200 Connection Established\r\n\r\n"); err != nil {
			return
		}
		if buffered.Flush() != nil {
			return
		}
		done := make(chan struct{})
		go func() { defer close(done); _, _ = io.Copy(remote, buffered); remote.Close() }()
		_, _ = io.Copy(local, remote)
		local.Close()
		remote.Close()
		<-done
	})
	go func() { _ = server.Serve(ln) }()
	var once sync.Once
	closeProxy := func() {
		once.Do(func() {
			cancel()
			_ = server.Close()
			mu.Lock()
			for c := range connections {
				_ = c.Close()
			}
			mu.Unlock()
		})
	}
	go func() { <-ctx.Done(); closeProxy() }()
	return "http://" + ln.Addr().String(), closeProxy, nil
}
