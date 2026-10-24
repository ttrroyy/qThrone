package probe

import (
	"context"
	"net/http"
	"time"

	"github.com/sagernet/sing-box/adapter"
)

var URLReporter resultBuffer[URLTestResult]

const URLTestTimeout = 3 * time.Second

type URLTestResult struct {
	Duration time.Duration
	Tag      string
	Error    error
}

func BatchURLTest(ctx context.Context, i Box, outboundTags []string, url string, maxConcurrency int, twice bool, timeout time.Duration) []*URLTestResult {
	results := BatchURLTestTo(ctx, i, outboundTags, url, maxConcurrency, twice, timeout, URLReporter.AddResult)
	URLReporter.Reclaim(results)
	return results
}

// BatchURLTestTo hands every finished result to publish; aborted tags are returned but never published.
func BatchURLTestTo(ctx context.Context, i Box, outboundTags []string, url string, maxConcurrency int, twice bool, timeout time.Duration, publish func(*URLTestResult), coldAllowance ...time.Duration) []*URLTestResult {
	if timeout <= 0 {
		timeout = URLTestTimeout
	}

	return runBatch(ctx, i, outboundTags, maxConcurrency, batchProbe[URLTestResult]{
		run: func(ctx context.Context, tag string, outbound adapter.Outbound) *URLTestResult {
			if err := awaitTunnels(ctx, i, tag); err != nil {
				return &URLTestResult{Tag: tag, Error: err}
			}
			client, closeClient := outboundHTTPClient(ctx, i, tag, outbound)
			defer closeClient()
			firstTimeout := firstRequestTimeout(i, tag, twice, timeout)
			if twice {
				firstTimeout += ColdRequestAllowance
			}
			if twice && len(coldAllowance) > 0 && coldAllowance[0] > 0 {
				firstTimeout += coldAllowance[0]
			}
			duration, err := urlTestRequests(ctx, client, url, timeout, firstTimeout, twice)
			return &URLTestResult{Duration: duration, Tag: tag, Error: err}
		},
		fail: func(tag string, err error) *URLTestResult {
			return &URLTestResult{Tag: tag, Error: err}
		},
		publish: publish,
	})
}

// The unmeasured first request also dials and handshakes the server, and a slow one must not read as unreachable.
const ColdRequestAllowance = 5 * time.Second

// A cold request that lost a packet waits out a retransmit (3 s for a SYN on Windows); a second one
// on a fresh connection usually gets through first. The measured request is never hedged.
const ColdRequestHedgeDelay = time.Second

func urlTestRequests(ctx context.Context, client *http.Client, url string, timeout, firstTimeout time.Duration, twice bool) (time.Duration, error) {
	if !twice {
		return urlTest(ctx, client, url, firstTimeout)
	}
	if _, err := hedgedURLTest(ctx, client, url, firstTimeout, ColdRequestHedgeDelay); err != nil {
		return 0, err
	}
	return urlTest(ctx, client, url, timeout)
}

// Each attempt keeps its own deadline; the first success wins and cancels the other.
func hedgedURLTest(ctx context.Context, client *http.Client, url string, timeout, delay time.Duration) (time.Duration, error) {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	type outcome struct {
		duration time.Duration
		err      error
	}
	results := make(chan outcome, 2)
	attempt := func() {
		duration, err := urlTest(ctx, client, url, timeout)
		results <- outcome{duration, err}
	}
	go attempt()
	hedge := time.NewTimer(delay)
	defer hedge.Stop()
	pending := 1
	for {
		select {
		case <-hedge.C:
			pending++
			go attempt()
		case res := <-results:
			pending--
			if res.err == nil || pending == 0 {
				return res.duration, res.err
			}
		}
	}
}

func urlTest(ctx context.Context, client *http.Client, url string, timeout time.Duration) (time.Duration, error) {
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	begin := time.Now()
	req, err := http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		return 0, err
	}
	resp, err := client.Do(req)
	if err != nil {
		return 0, err
	}
	_ = resp.Body.Close()
	return time.Since(begin), nil
}
