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

func urlTestRequests(ctx context.Context, client *http.Client, url string, timeout, firstTimeout time.Duration, twice bool) (time.Duration, error) {
	duration, err := urlTest(ctx, client, url, firstTimeout)
	if err == nil && twice {
		return urlTest(ctx, client, url, timeout)
	}
	return duration, err
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
