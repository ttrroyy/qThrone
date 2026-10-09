package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/url"
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

var errCaptchaWindowClosed = errors.New("VK captcha window was closed")

var captchaWindow = make(chan struct{}, 1)

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

func vkCaptchaURL(raw string) bool {
	u, err := url.Parse(raw)
	if err != nil || u.Scheme != "https" || u.User != nil {
		return false
	}
	h := strings.ToLower(u.Hostname())
	return h == "vk.com" || h == "vk.ru" || strings.HasSuffix(h, ".vk.com") || strings.HasSuffix(h, ".vk.ru")
}

func captchaSuccessToken(body []byte) string {
	var result struct {
		Response struct {
			SuccessToken string `json:"success_token"`
		} `json:"response"`
	}
	if json.Unmarshal(body, &result) != nil {
		return ""
	}
	return result.Response.SuccessToken
}

// Mirrors Android's captchaNotRobot.check response handling. The browser owns
// a temporary profile; existing browser tabs, accounts and cookies are untouched.
func solveDesktopCaptcha(parent context.Context, redirect string) (string, error) {
	if err := parent.Err(); err != nil {
		return "", err
	}
	if !vkCaptchaURL(redirect) {
		return "", errors.New("invalid VK captcha URL")
	}
	select {
	case captchaWindow <- struct{}{}:
		defer func() { <-captchaWindow }()
	case <-parent.Done():
		return "", parent.Err()
	}
	ctx, cancel := context.WithTimeout(parent, 3*time.Minute)
	defer cancel()
	profile, err := os.MkdirTemp("", "qthrone-captcha-")
	if err != nil {
		return "", errors.New("cannot create private captcha browser profile")
	}
	defer os.RemoveAll(profile)
	opts := append([]chromedp.ExecAllocatorOption{}, chromedp.DefaultExecAllocatorOptions[:]...)
	if edge := edgeExecutable(); edge != "" {
		opts = append(opts, chromedp.ExecPath(edge))
	}
	opts = append(opts, chromedp.Flag("headless", false), chromedp.UserDataDir(profile), chromedp.Flag("app", "about:blank"), chromedp.Flag("remote-debugging-address", "127.0.0.1"))
	alloc, closeAlloc := chromedp.NewExecAllocator(ctx, opts...)
	defer closeAlloc()
	page, closePage := chromedp.NewContext(alloc)
	defer closePage()
	token := make(chan string, 1)
	var mu sync.Mutex
	pending := make(map[network.RequestID]bool)
	chromedp.ListenTarget(page, func(event any) {
		switch e := event.(type) {
		case *network.EventResponseReceived:
			u, er := url.Parse(e.Response.URL)
			if er == nil && vkCaptchaURL(e.Response.URL) && strings.Contains(u.Path, "captchaNotRobot.check") {
				mu.Lock()
				pending[e.RequestID] = true
				mu.Unlock()
			}
		case *network.EventLoadingFinished:
			mu.Lock()
			matched := pending[e.RequestID]
			delete(pending, e.RequestID)
			mu.Unlock()
			if !matched {
				return
			}
			go func() {
				var body []byte
				if chromedp.Run(page, chromedp.ActionFunc(func(c context.Context) error {
					var er error
					body, er = network.GetResponseBody(e.RequestID).Do(c)
					return er
				})) != nil {
					return
				}
				if value := captchaSuccessToken(body); value != "" {
					select {
					case token <- value:
					default:
					}
				}
			}()
		}
	})
	if chromedp.Run(page, network.Enable(), chromedp.Navigate(redirect)) != nil {
		return "", fmt.Errorf("cannot open VK captcha: Chrome, Edge or Chromium is required")
	}
	select {
	case value := <-token:
		return value, nil
	case <-page.Done():
		if parent.Err() != nil {
			return "", parent.Err()
		}
		if ctx.Err() != nil {
			return "", ctx.Err()
		}
		return "", errCaptchaWindowClosed
	}
}
