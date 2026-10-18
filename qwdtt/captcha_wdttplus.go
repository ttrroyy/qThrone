package main

import (
	"context"
	"errors"
	"math/rand"
	"strings"
	"time"

	"github.com/chromedp/cdproto/input"
	"github.com/chromedp/chromedp"
)

// WDTT Plus solves VK's captcha its own way: the hidden attempt gets 18 s and the
// visible window 180 s, the page settles 900-1700 ms, up to 16 probes 350 ms apart
// look for the checkbox (its icon first), 600-1300 ms of "thinking" precede a press
// held 150-320 ms, and two watchdogs follow the press: 3.5 s for the check request
// to leave, 8 s for an answer. BOT, ERROR_LIMIT and ERROR verdicts end it at once.
const (
	wdttPlusAutoTimeout    = 18 * time.Second
	wdttPlusVisibleTimeout = 180 * time.Second
	wdttPlusProbeInterval  = 350 * time.Millisecond
	wdttPlusProbes         = 16
	wdttPlusCheckWatchdog  = 3500 * time.Millisecond
	wdttPlusResultWatchdog = 8 * time.Second
)

// wdttPlusCheckbox answers "manual" for a slider, else [kind, left, top, width, height].
const wdttPlusCheckbox = `(() => {
	if (document.querySelector('[class*="SliderCaptcha"], [class*="Kaleidoscope"], .vkc__SwipeButton-module__track, [class*="SwipeButton"]')) return 'manual';
	const visible = n => { if (!n) return null; const r = n.getBoundingClientRect(), s = getComputedStyle(n); return (r.width < 5 || r.height < 5 || s.display === 'none' || s.visibility === 'hidden') ? null : r; };
	const el = document.querySelector('label.vkc__Checkbox-module__Checkbox') || document.querySelector('label[for="not-robot-captcha-checkbox"]') || document.getElementById('not-robot-captcha-checkbox');
	if (!el) return null;
	const icon = visible(el.querySelector && el.querySelector('[class*="Checkbox__iconBlock"], [class*="iconBlock"], [class*="IconBlock"]'));
	if (icon) return ['icon', icon.left, icon.top, icon.width, icon.height];
	const r = visible(el);
	return r ? ['label', r.left, r.top, r.width, r.height] : null;
})()`

const wdttPlusSliderAfterClick = `!!document.querySelector('[class*="SliderCaptcha"], [class*="Kaleidoscope"], .vkc__SliderCaptcha-module__description, .vkc__KaleidoscopeScreen-module__captchaId, .vkc__SwipeButton-module__track')`

func wdttPlusSettle(random *rand.Rand) time.Duration {
	return time.Duration(900+random.Intn(800)) * time.Millisecond
}

func wdttPlusThink(random *rand.Rand) time.Duration {
	return time.Duration(600+random.Intn(700)) * time.Millisecond
}

func wdttPlusHold(random *rand.Rand) time.Duration {
	return time.Duration(150+random.Intn(170)) * time.Millisecond
}

// wdttPlusClickPoint aims inside the checkbox icon, or at the box on the label's left edge.
func wdttPlusClickPoint(random *rand.Rand, kind string, left, top, width, height float64) (float64, float64) {
	x := left + width*(0.25+random.Float64()*0.5)
	if kind != "icon" {
		x = left + min(height*(0.35+random.Float64()*0.3), width*0.35)
	}
	return x, top + height*(0.25+random.Float64()*0.5)
}

// wdttPlusVerdictFails is WDTT Plus's reading of captchaNotRobot.check statuses.
func wdttPlusVerdictFails(status string) bool {
	switch strings.ToUpper(status) {
	case "BOT", "ERROR_LIMIT", "ERROR":
		return true
	}
	return false
}

func pressWdttPlusCheckbox(ctx context.Context, random *rand.Rand, kind string, box []float64) error {
	x, y := wdttPlusClickPoint(random, kind, box[0], box[1], box[2], box[3])
	if err := chromedp.Run(ctx, chromedp.ActionFunc(func(c context.Context) error {
		if err := input.DispatchMouseEvent(input.MouseMoved, x, y).Do(c); err != nil {
			return err
		}
		return input.DispatchMouseEvent(input.MousePressed, x, y).WithButton(input.Left).WithClickCount(1).Do(c)
	})); err != nil {
		return err
	}
	hold := wdttPlusHold(random)
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-time.After(hold / 2):
	}
	x2, y2 := x+random.Float64()*1.6-0.8, y+random.Float64()*1.2-0.6
	if err := chromedp.Run(ctx, chromedp.ActionFunc(func(c context.Context) error {
		return input.DispatchMouseEvent(input.MouseMoved, x2, y2).WithButton(input.Left).Do(c)
	})); err != nil {
		return err
	}
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-time.After(hold - hold/2):
	}
	return chromedp.Run(ctx, chromedp.ActionFunc(func(c context.Context) error {
		return input.DispatchMouseEvent(input.MouseReleased, x+random.Float64()*2-1, y+random.Float64()-0.5).
			WithButton(input.Left).WithClickCount(1).Do(c)
	}))
}

func watchWdttPlusCaptchaPage(ctx context.Context, tab context.Context, visible bool, token <-chan string) (string, error) {
	random := rand.New(rand.NewSource(time.Now().UnixNano()))
	budget := wdttPlusVisibleTimeout
	if !visible {
		budget = wdttPlusAutoTimeout
	}
	solve, cancel := context.WithTimeout(ctx, budget)
	defer cancel()
	settleAt := time.Now().Add(wdttPlusSettle(random))
	var clickedAt time.Time
	probes := 0
	ticker := time.NewTicker(wdttPlusProbeInterval)
	defer ticker.Stop()
	for {
		select {
		case value := <-token:
			return value, nil
		case <-tab.Done():
			if ctx.Err() != nil {
				return "", ctx.Err()
			}
			return "", errCaptchaWindowClosed
		case <-solve.Done():
			if ctx.Err() == nil {
				return "", errors.New("webview captcha timed out")
			}
			return "", ctx.Err()
		case <-ticker.C:
		}
		state, err := readCaptchaState(tab)
		if err == nil {
			if state.Token != nil && *state.Token != "" {
				return *state.Token, nil
			}
			if !visible && wdttPlusVerdictFails(state.Status) {
				return "", errors.New("webview captcha failed: captcha_check_" + strings.ToUpper(state.Status))
			}
			if state.Failure != nil && *state.Failure == "slider" && !visible {
				return "", errCaptchaSlider
			}
			if state.Failure != nil && *state.Failure == "error" {
				return "", errors.New("webview captcha failed: VK captcha error")
			}
			if state.Dismissed && visible {
				return "", errCaptchaWindowClosed
			}
		}
		if visible {
			continue
		}
		if !clickedAt.IsZero() {
			since := time.Since(clickedAt)
			if since >= wdttPlusCheckWatchdog && err == nil && !state.CheckSent {
				return "", errors.New("webview captcha failed: auto_check_not_sent")
			}
			if since >= wdttPlusResultWatchdog {
				return "", errors.New("webview captcha failed: auto_no_result")
			}
			var slider bool
			if since <= 5400*time.Millisecond && chromedp.Run(tab, chromedp.Evaluate(wdttPlusSliderAfterClick, &slider)) == nil && slider {
				return "", errCaptchaSlider
			}
			continue
		}
		if time.Now().Before(settleAt) {
			continue
		}
		var target any
		if chromedp.Run(tab, chromedp.Evaluate(wdttPlusCheckbox, &target)) != nil {
			target = nil
		}
		if value, ok := target.(string); ok && value == "manual" {
			return "", errCaptchaSlider
		}
		kind, box := wdttPlusTarget(target)
		if box == nil {
			if probes++; probes >= wdttPlusProbes {
				return "", errors.New("webview captcha failed: checkbox_not_found")
			}
			continue
		}
		select {
		case <-solve.Done():
			continue
		case <-time.After(wdttPlusThink(random)):
		}
		if pressWdttPlusCheckbox(tab, random, kind, box) == nil {
			clickedAt = time.Now()
		}
	}
}

// wdttPlusTarget reads ["icon"|"label", left, top, width, height] from the page.
func wdttPlusTarget(value any) (string, []float64) {
	list, ok := value.([]any)
	if !ok || len(list) != 5 {
		return "", nil
	}
	kind, ok := list[0].(string)
	if !ok || (kind != "icon" && kind != "label") {
		return "", nil
	}
	box := make([]float64, 4)
	for i, v := range list[1:] {
		number, ok := v.(float64)
		if !ok {
			return "", nil
		}
		box[i] = number
	}
	return kind, box
}
