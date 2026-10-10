package main

import (
	"context"
	"errors"
	"io"
	"net"
	"strings"
	"testing"
	"time"
)

func TestTURNConnectHonorsCancelledContext(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	for _, tcp := range []bool{false, true} {
		_, _, err := dialTURNConn(ctx, "not-resolved.invalid:3478", tcp)
		if !errors.Is(err, context.Canceled) {
			t.Fatalf("TURN dial ignored cancellation: %v", err)
		}
	}
}

func TestTURNAllocationCanBeStopped(t *testing.T) {
	ln, err := net.ListenPacket("udp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() {
		_, err := RunSession(ctx, &TurnParams{}, &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: 56003}, nil, "9000", false, nil, 1,
			&Credentials{TurnURLs: []string{ln.LocalAddr().String()}, User: "fake", Pass: "fake"}, "fake-device", "fake-password", &Stats{}, nil)
		done <- err
	}()
	_ = ln.SetReadDeadline(time.Now().Add(time.Second))
	if _, _, err := ln.ReadFrom(make([]byte, 1500)); err != nil {
		t.Fatal(err)
	}
	cancel()
	select {
	case err := <-done:
		if err == nil {
			t.Fatal("cancelled allocation succeeded")
		}
	case <-time.After(time.Second):
		t.Fatal("TURN allocation kept waiting after Stop")
	}
}

func TestRAWDisconnectCompletesBeforeRelayCleanup(t *testing.T) {
	client, server := net.Pipe()
	defer client.Close()
	defer server.Close()
	ctx, cancel := context.WithCancel(context.Background())
	finish := stopSessionIO(ctx, ctx, client, true, "fake-device")
	cancel()
	_ = server.SetReadDeadline(time.Now().Add(time.Second))
	want := "DISCONNECT_RAW:fake-device"
	b := make([]byte, len(want))
	if _, err := io.ReadFull(server, b); err != nil || string(b) != want {
		t.Fatalf("disconnect before cleanup: %q %v", b, err)
	}
	finish()
}

func TestRAWConfigWaitCanBeStopped(t *testing.T) {
	client, server := net.Pipe()
	defer client.Close()
	defer server.Close()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	finish := stopSessionIO(ctx, ctx, client, true, "fake-device")
	defer finish()
	done := make(chan error, 1)
	go func() {
		_, _, _, err := RequestRawConfig(client, "fake-device", "fake-password")
		done <- err
	}()
	_ = server.SetReadDeadline(time.Now().Add(time.Second))
	request := "GETCONF_RAW:fake-device|fake-password"
	buf := make([]byte, len(request))
	if _, err := io.ReadFull(server, buf); err != nil || string(buf) != request {
		t.Fatalf("configuration request: %q %v", buf, err)
	}
	cancel()
	disconnect := "DISCONNECT_RAW:fake-device"
	buf = make([]byte, len(disconnect))
	if _, err := io.ReadFull(server, buf); err != nil || string(buf) != disconnect {
		t.Fatalf("disconnect while waiting for config: %q %v", buf, err)
	}
	select {
	case err := <-done:
		if err == nil {
			t.Fatal("cancelled configuration request succeeded")
		}
	case <-time.After(time.Second):
		t.Fatal("configuration request kept waiting after Stop")
	}
}

func TestDesktopCaptchaResponseAndURLValidation(t *testing.T) {
	if captchaSuccessToken([]byte(`{"response":{"success_token":"fake-success"}}`)) != "fake-success" {
		t.Fatal("captcha response was not decoded")
	}
	for _, body := range []string{`{}`, `{"error":{"error_code":14}}`, `not-json`} {
		if captchaSuccessToken([]byte(body)) != "" {
			t.Fatal("invalid captcha result accepted")
		}
	}
	for _, u := range []string{"https://id.vk.ru/captcha?session_token=fake", "https://api.vk.com/method/captchaNotRobot.check"} {
		if !vkCaptchaURL(u) {
			t.Fatal("VK URL was rejected")
		}
	}
	for _, u := range []string{"http://id.vk.ru/captcha", "https://vk.com.evil.invalid/captcha", "https://127.0.0.1/captcha", "https://user:pass@id.vk.ru/captcha"} {
		if vkCaptchaURL(u) {
			t.Fatal("non-VK captcha URL accepted")
		}
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	_, err := solveDesktopCaptcha(ctx, "https://id.vk.ru/captcha?session_token=fake")
	if err == nil || !strings.Contains(err.Error(), "canceled") {
		t.Fatal("cancelled captcha attempted to open a browser")
	}
}

func TestManualProbeOpensVisibleCaptcha(t *testing.T) {
	oldBridge, oldProbe, oldInteractive := desktopBridgeMode, desktopProbeOnly, desktopInteractiveProbe
	oldSolver := desktopCaptchaSolver
	defer func() {
		desktopBridgeMode, desktopProbeOnly, desktopInteractiveProbe = oldBridge, oldProbe, oldInteractive
		desktopCaptchaSolver = oldSolver
	}()
	desktopBridgeMode, desktopProbeOnly, desktopInteractiveProbe = true, true, true
	called := false
	desktopCaptchaSolver = func(ctx context.Context, redirect string, visible bool) (string, error) {
		called = true
		if !visible || redirect != "https://id.vk.ru/captcha?session_token=fake" {
			t.Fatal("manual probe did not request its visible captcha")
		}
		return "fake-success", nil
	}
	token, err := requestWebViewCaptcha(1, &VkCaptchaError{RedirectURI: "https://id.vk.ru/captcha?session_token=fake"}, "auto", time.Second)
	if !called || err != nil || token != "fake-success" {
		t.Fatalf("manual captcha result: %q %v", token, err)
	}
}
