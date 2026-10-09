package main

import (
	"context"
	"io"
	"net"
	"net/http"
	"sync"
	"time"
)

// Only the private captcha browser uses this short-lived HTTPS proxy. Its
// connections belong to qwdtt, which Throne already excludes from its TUN.
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
