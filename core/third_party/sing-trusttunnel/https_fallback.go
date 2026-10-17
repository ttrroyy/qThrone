package trusttunnel

import (
	"context"
	"net"
	"net/http"
	"sync"
	"time"
)

// fallbackDelay is TrustTunnelClient's VPN_DEFAULT_FALLBACK_CONNECT_DELAY_MS.
const fallbackDelay = time.Second

// httpsFallbackTransport races the QUIC endpoint and its HTTP/2 fallback the
// way TrustTunnelClient's FallbackableUpstreamConnector does: QUIC starts
// first, HTTP/2 joins after fallbackDelay or as soon as QUIC fails, and the
// first transport to answer carries the requests. A transport that stops
// answering makes the next request race again. Application data is never
// replayed: the race uses health checks only.
type httpsFallbackTransport struct {
	access   sync.Mutex
	quic     RoundTripper
	https    *Client
	owner    *Client
	selected RoundTripper
	racing   chan struct{}
	closed   bool
}

func (c *Client) enableHTTPSFallback(options ClientOptions) error {
	options.QUIC = false
	options.HealthCheck = false
	options.TLSConfig = options.TLSConfig.Clone()
	options.TLSConfig.SetNextProtos([]string{"h2"})
	https, err := NewClient(options)
	if err != nil {
		return err
	}
	c.roundTripper = &httpsFallbackTransport{quic: c.roundTripper, https: https, owner: c}
	return nil
}

// probe asks one transport for the health check. Any HTTP answer proves the
// transport, even a rejected authentication: retrying that over the other
// transport would only hide it.
func probe(ctx context.Context, transport RoundTripper, client *Client) error {
	request := newRequest(client.server.String(), HealthCheckMagicAddress, nil).WithContext(ctx)
	request.Header.Set("Proxy-Authorization", client.auth)
	request.Header.Set("User-Agent", client.userAgents.HealthCheckUserAgent)
	response, err := transport.RoundTrip(request)
	if response != nil {
		response.Body.Close()
	}
	return err
}

func (t *httpsFallbackTransport) race(ctx context.Context) (RoundTripper, error) {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	type result struct {
		transport RoundTripper
		err       error
	}
	results := make(chan result, 2)
	go func() { results <- result{t.quic, probe(ctx, t.quic, t.owner)} }()
	pending, fallbackStarted := 1, false
	startFallback := func() {
		if !fallbackStarted {
			fallbackStarted = true
			pending++
			go func() { results <- result{t.https.roundTripper, probe(ctx, t.https.roundTripper, t.https)} }()
		}
	}
	timer := time.NewTimer(fallbackDelay)
	defer timer.Stop()
	var lastErr error
	for {
		select {
		case <-timer.C:
			startFallback()
		case r := <-results:
			pending--
			if r.err == nil {
				return r.transport, nil
			}
			lastErr = r.err
			// The main protocol failed: try the fallback immediately.
			startFallback()
			if pending == 0 {
				return nil, lastErr
			}
		case <-ctx.Done():
			return nil, ctx.Err()
		}
	}
}

func (t *httpsFallbackTransport) RoundTrip(request *http.Request) (*http.Response, error) {
	t.access.Lock()
	for t.selected == nil {
		if t.closed {
			t.access.Unlock()
			return nil, net.ErrClosed
		}
		if racing := t.racing; racing != nil {
			t.access.Unlock()
			select {
			case <-racing:
			case <-request.Context().Done():
				return nil, request.Context().Err()
			}
			t.access.Lock()
			continue
		}
		racing := make(chan struct{})
		t.racing = racing
		t.access.Unlock()
		winner, err := t.race(request.Context())
		t.access.Lock()
		t.racing = nil
		close(racing)
		if err != nil {
			t.access.Unlock()
			return nil, err
		}
		if t.closed {
			continue
		}
		t.selected = winner
		// The losing connection is closed, as the original disconnects the other connector.
		if winner == t.quic {
			t.https.ResetConnections()
		} else {
			t.quic.CloseIdleConnections()
			if tracker := t.owner.connTracker; tracker != nil {
				_ = tracker.Close()
			}
		}
	}
	selected := t.selected
	t.access.Unlock()
	response, err := selected.RoundTrip(request)
	if err != nil && request.Context().Err() == nil {
		// The chosen transport stopped answering: the next request races again.
		t.access.Lock()
		if t.selected == selected {
			t.selected = nil
		}
		t.access.Unlock()
	}
	return response, err
}

func (t *httpsFallbackTransport) CloseIdleConnections() {
	t.access.Lock()
	defer t.access.Unlock()
	t.quic.CloseIdleConnections()
	t.https.ResetConnections()
	t.selected = nil
}

func (t *httpsFallbackTransport) Close() error {
	t.access.Lock()
	defer t.access.Unlock()
	t.closed = true
	if closer, ok := t.quic.(interface{ Close() error }); ok {
		_ = closer.Close()
	}
	return t.https.Close()
}
