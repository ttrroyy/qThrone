package main

import (
	"context"
	"encoding/json"
	"strings"
	"testing"
)

func TestCSQTTFatalRejectionCancelsBeforeReady(t *testing.T) {
	for _, marker := range []string{"FATAL_AUTH: пароль привязан к другому устройству", "FATAL_PROTOCOL: incompatible server"} {
		ctx, cancel := context.WithCancel(context.Background())
		configs := make(chan string, 1)
		scanCSQTTOutput(ctx, &bridgeConfig{}, strings.NewReader(marker+"\n__CSQTT_EVENT__|CONFIG|{\"config\":\"unexpected\"}\n"), configs, func(string) {}, cancel)
		if ctx.Err() == nil {
			t.Fatal("fatal server rejection did not stop session")
		}
		if len(configs) != 0 {
			t.Fatal("rejected session continued to readiness")
		}
		cancel()
	}
}

func TestCSQTTBackgroundCaptchaDoesNotOpenBrowser(t *testing.T) {
	previous := desktopCaptchaSolver
	defer func() { desktopCaptchaSolver = previous }()
	desktopCaptchaSolver = func(context.Context, string, bool) (string, error) {
		t.Error("background probe opened browser")
		return "", nil
	}
	config := &bridgeConfig{ProbeOnly: true}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	var commands []string
	scanCSQTTOutput(ctx, config, strings.NewReader("CAPTCHA_SOLVE|manual|https://vk.com/captcha|fake-session\n"), make(chan string, 1), func(command string) { commands = append(commands, command) }, cancel)
	if len(commands) != 1 || commands[0] != "CAPTCHA_RESULT|error:cancelled" {
		t.Fatal("background captcha not cancelled")
	}
	if ctx.Err() == nil {
		t.Fatal("background probe kept reconnecting after captcha")
	}
}

func TestCSQTTCaptchaWindowCloseCancelsSession(t *testing.T) {
	previous := desktopCaptchaSolver
	defer func() { desktopCaptchaSolver = previous }()
	desktopCaptchaSolver = func(context.Context, string, bool) (string, error) { return "", errCaptchaWindowClosed }
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	scanCSQTTOutput(ctx, &bridgeConfig{InteractiveCaptcha: true}, strings.NewReader("CAPTCHA_SOLVE|manual|https://vk.com/captcha|fake-session\n"), make(chan string, 1), func(string) {}, cancel)
	if ctx.Err() == nil {
		t.Fatal("closed captcha window did not cancel transport")
	}
}

func TestCSQTTManualCaptchaAndConfigEvents(t *testing.T) {
	previous := desktopCaptchaSolver
	defer func() { desktopCaptchaSolver = previous }()
	desktopCaptchaSolver = func(_ context.Context, redirect string, visible bool) (string, error) {
		if !visible || redirect != "https://vk.com/captcha" {
			t.Error("invalid browser request")
		}
		return "fake-token", nil
	}
	configs := make(chan string, 1)
	commands := make(chan string, 1)
	event, _ := json.Marshal(map[string]string{"config": "TUNCONF:10.66.67.42:1.1.1.1:19000:stream-v2"})
	logs := "[time] __CSQTT_EVENT__|CONFIG|" + string(event) + "\nCAPTCHA_SOLVE|manual|https://vk.com/captcha|fake-session\n"
	scanCSQTTOutput(context.Background(), &bridgeConfig{ProbeOnly: true, InteractiveCaptcha: true}, strings.NewReader(logs), configs, func(command string) { commands <- command })
	if len(configs) != 1 || <-commands != "CAPTCHA_RESULT|fake-token" {
		t.Fatal("transport event not handled")
	}
}
