package main

import (
	"context"
	"encoding/json"
	"errors"
	"math/rand"
	"time"

	"github.com/chromedp/cdproto/input"
	"github.com/chromedp/cdproto/page"
	"github.com/chromedp/chromedp"
)

// The automatic attempt and the visible window follow the original Android
// WebView: the hidden attempt presses VK's checkbox like a person and gives up
// at once on a slider; in the visible window VK's own close cross declines.

var errCaptchaSlider = errors.New("webview captcha failed: slider requires the manual window")

// captchaIntercept reads VK's captchaNotRobot.check answers inside the page and
// notices VK's own close cross, as the original window does.
const captchaIntercept = `(() => {
	if (window.__qthroneCaptchaHook) return; window.__qthroneCaptchaHook = true;
	const receive = text => { try {
		const data = JSON.parse(text);
		const token = data.response && data.response.success_token;
		if (typeof token === 'string' && token.length > 0) { window.__qthroneCaptchaToken = token; return; }
		if (data.response && data.response.status) window.__qthroneCaptchaStatus = String(data.response.status);
		if (data.response && data.response.show_captcha_type === 'slider') window.__qthroneCaptchaFailure = 'slider';
		else if (data.error) window.__qthroneCaptchaFailure = 'error';
	} catch (e) {} };
	const isCheck = u => String(u || '').includes('captchaNotRobot.check');
	const fetchOriginal = window.fetch;
	window.fetch = async function(...args) { const input = args[0]; if (isCheck(typeof input === 'string' ? input : (input && input.url))) window.__qthroneCheckSent = true; const response = await fetchOriginal.apply(this, args); if (response.url.includes('captchaNotRobot.check')) response.clone().text().then(receive); return response; };
	const openOriginal = XMLHttpRequest.prototype.open;
	XMLHttpRequest.prototype.open = function(method, url, ...rest) { this.__qthroneUrl = String(url || ''); return openOriginal.call(this, method, url, ...rest); };
	const sendOriginal = XMLHttpRequest.prototype.send;
	XMLHttpRequest.prototype.send = function(...args) { if (isCheck(this.__qthroneUrl)) window.__qthroneCheckSent = true; this.addEventListener('load', () => { if (this.responseURL.includes('captchaNotRobot.check') && (!this.responseType || this.responseType === 'text')) receive(this.responseText); }); return sendOriginal.apply(this, args); };
	document.addEventListener('click', e => { if (e.target.closest && e.target.closest('.vkc__ModalCardBase-module__dismiss')) window.__qthroneCaptchaDismissed = true; }, true);
})();`

const captchaState = `JSON.stringify({token: window.__qthroneCaptchaToken || null, failure: window.__qthroneCaptchaFailure || null, dismissed: !!window.__qthroneCaptchaDismissed, checkSent: !!window.__qthroneCheckSent, status: window.__qthroneCaptchaStatus || ''})`

// captchaCheckbox answers "manual" for a slider or kaleidoscope, else the checkbox label as [left, top, width, height].
const captchaCheckbox = `(() => {
	if (document.querySelector('[class*="SliderCaptcha"], [class*="Kaleidoscope"]')) return 'manual';
	const el = document.querySelector('label.vkc__Checkbox-module__Checkbox') || document.querySelector('label[for="not-robot-captcha-checkbox"]') || document.getElementById('not-robot-captcha-checkbox');
	if (!el) return null;
	const r = el.getBoundingClientRect(), s = getComputedStyle(el);
	if (r.width < 5 || r.height < 5 || s.display === 'none' || s.visibility === 'hidden') return null;
	return [r.left, r.top, r.width, r.height];
})()`

const captchaSliderAfterClick = `!!document.querySelector('[class*="SliderCaptcha"], [class*="Kaleidoscope"], .vkc__SwipeButton-module__track')`

// captchaClickDelay is the original's pause: the page settles for 650-1200 ms, then 420-680 ms pass before the touch.
func captchaClickDelay(random *rand.Rand) time.Duration {
	return time.Duration(650+random.Intn(550)+420+random.Intn(260)) * time.Millisecond
}

// captchaClickPoint is a random point inside the label, as the original touches it.
func captchaClickPoint(random *rand.Rand, left, top, width, height float64) (float64, float64) {
	return left + width*(0.15+random.Float64()*0.7), top + height*(0.25+random.Float64()*0.5)
}

func installCaptchaIntercept(ctx context.Context) error {
	_, err := page.AddScriptToEvaluateOnNewDocument(captchaIntercept).Do(ctx)
	return err
}

type captchaPageState struct {
	Token     *string `json:"token"`
	Failure   *string `json:"failure"`
	Dismissed bool    `json:"dismissed"`
	CheckSent bool    `json:"checkSent"`
	Status    string  `json:"status"`
}

func readCaptchaState(ctx context.Context) (captchaPageState, error) {
	var raw string
	var state captchaPageState
	if err := chromedp.Run(ctx, chromedp.Evaluate(captchaState, &raw)); err != nil {
		return state, err
	}
	err := json.Unmarshal([]byte(raw), &state)
	return state, err
}

// pressCaptchaCheckbox presses the label with a held mouse button, released with a slight drift.
func pressCaptchaCheckbox(ctx context.Context, random *rand.Rand, label []float64) error {
	x, y := captchaClickPoint(random, label[0], label[1], label[2], label[3])
	if err := chromedp.Run(ctx, chromedp.ActionFunc(func(c context.Context) error {
		if err := input.DispatchMouseEvent(input.MouseMoved, x, y).Do(c); err != nil {
			return err
		}
		return input.DispatchMouseEvent(input.MousePressed, x, y).WithButton(input.Left).WithClickCount(1).Do(c)
	})); err != nil {
		return err
	}
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-time.After(time.Duration(80+random.Intn(100)) * time.Millisecond):
	}
	return chromedp.Run(ctx, chromedp.ActionFunc(func(c context.Context) error {
		return input.DispatchMouseEvent(input.MouseReleased, x+random.Float64()*2-1, y+random.Float64()-0.5).
			WithButton(input.Left).WithClickCount(1).Do(c)
	}))
}

// watchCaptchaPage drives the page until VK answers. The hidden attempt has
// solveBudget from the moment the page loaded, the original's 10 s.
func watchCaptchaPage(ctx context.Context, tab context.Context, visible bool, solveBudget time.Duration, token <-chan string) (string, error) {
	random := rand.New(rand.NewSource(time.Now().UnixNano()))
	solve := ctx
	if !visible {
		var cancel context.CancelFunc
		solve, cancel = context.WithTimeout(ctx, solveBudget)
		defer cancel()
	}
	var checkboxSeen, clickedAt time.Time
	ticker := time.NewTicker(150 * time.Millisecond)
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
			if !visible && ctx.Err() == nil {
				return "", errors.New("webview captcha timed out")
			}
			return "", solve.Err()
		case <-ticker.C:
		}
		state, err := readCaptchaState(tab)
		if err == nil {
			if state.Token != nil && *state.Token != "" {
				return *state.Token, nil
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
			// As the original: for about five seconds after the click a slider means a manual captcha.
			var slider bool
			if time.Since(clickedAt) <= 5400*time.Millisecond && chromedp.Run(tab, chromedp.Evaluate(captchaSliderAfterClick, &slider)) == nil && slider {
				return "", errCaptchaSlider
			}
			continue
		}
		var label any
		if chromedp.Run(tab, chromedp.Evaluate(captchaCheckbox, &label)) != nil {
			continue
		}
		switch value := label.(type) {
		case string:
			if value == "manual" {
				return "", errCaptchaSlider
			}
		case []any:
			if len(value) != 4 {
				continue
			}
			if checkboxSeen.IsZero() {
				checkboxSeen = time.Now().Add(captchaClickDelay(random))
			}
			if time.Now().Before(checkboxSeen) {
				continue
			}
			box := make([]float64, 4)
			for i, v := range value {
				number, ok := v.(float64)
				if !ok {
					box = nil
					break
				}
				box[i] = number
			}
			if box != nil && pressCaptchaCheckbox(tab, random, box) == nil {
				clickedAt = time.Now()
			}
		}
	}
}
