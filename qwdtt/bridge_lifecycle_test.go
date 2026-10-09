package main

import (
	"context"
	"io"
	"net"
	"strings"
	"testing"
	"time"
)

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
