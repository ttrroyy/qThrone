package main

import (
	"bufio"
	"context"
	"io"
	"net"
	"net/http"
	"strings"
	"testing"
	"time"
)

func TestCaptchaProxyConnectAndCancellation(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	proxy, closeProxy, err := startCaptchaProxyWithDial(ctx, func(ctx context.Context, network, address string) (net.Conn, error) {
		if network != "tcp4" || address != "captcha.invalid:443" {
			t.Errorf("unexpected destination %s %s", network, address)
		}
		local, peer := net.Pipe()
		go func() { defer peer.Close(); _, _ = io.Copy(peer, peer) }()
		return local, nil
	})
	if err != nil {
		t.Fatal(err)
	}
	defer closeProxy()
	c, err := net.Dial("tcp", strings.TrimPrefix(proxy, "http://"))
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	_ = c.SetDeadline(time.Now().Add(3 * time.Second))
	_, _ = io.WriteString(c, "CONNECT captcha.invalid:443 HTTP/1.1\r\nHost: captcha.invalid:443\r\n\r\n")
	r := bufio.NewReader(c)
	resp, err := http.ReadResponse(r, &http.Request{Method: "CONNECT"})
	if err != nil || resp.StatusCode != 200 {
		t.Fatalf("CONNECT: %v %v", resp, err)
	}
	_, _ = io.WriteString(c, "captcha")
	b := make([]byte, 7)
	if _, err := io.ReadFull(r, b); err != nil || string(b) != "captcha" {
		t.Fatalf("relay: %q %v", b, err)
	}
	cancel()
	if _, err := r.ReadByte(); err == nil {
		t.Fatal("cancelled browser connection remained open")
	}
	closeProxy()
	if conn, err := net.DialTimeout("tcp", strings.TrimPrefix(proxy, "http://"), time.Second); err == nil {
		conn.Close()
		t.Fatal("proxy listener remained open")
	}
}

func TestCaptchaProxyRejectsPlainHTTP(t *testing.T) {
	proxy, closeProxy, err := startCaptchaProxy(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	defer closeProxy()
	c, err := net.Dial("tcp", strings.TrimPrefix(proxy, "http://"))
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	_ = c.SetDeadline(time.Now().Add(time.Second))
	_, _ = io.WriteString(c, "GET http://captcha.invalid/ HTTP/1.1\r\nHost: captcha.invalid\r\n\r\n")
	resp, err := http.ReadResponse(bufio.NewReader(c), nil)
	if err != nil || resp.StatusCode != http.StatusForbidden {
		t.Fatalf("plain HTTP: %v %v", resp, err)
	}
	resp.Body.Close()
}

func TestBackgroundCaptchaDoesNotLaunchBrowser(t *testing.T) {
	oldBridge, oldProbe, oldCancel := desktopBridgeMode, desktopProbeOnly, bridgeCancel
	defer func() { desktopBridgeMode, desktopProbeOnly, bridgeCancel = oldBridge, oldProbe, oldCancel }()
	desktopBridgeMode, desktopProbeOnly = true, true
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	bridgeCancel = cancel
	_, err := solveCaptchaBySelectedMode(ctx, 1, 1, nil, nil, Profile{}, nil)
	if err != errCaptchaInteractionRequired || ctx.Err() != context.Canceled {
		t.Fatalf("background captcha: %v %v", err, ctx.Err())
	}
}
