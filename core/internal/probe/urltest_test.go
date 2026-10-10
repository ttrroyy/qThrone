package probe

import (
	"context"
	"net/http"
	"net/http/httptest"
	"sync/atomic"
	"testing"
	"time"
)

func TestURLWarmupDoesNotChangeMeasuredTimeout(t *testing.T) {
	for _, slowMeasured := range []bool{false, true} {
		t.Run(map[bool]string{false: "cold-start", true: "measured-timeout"}[slowMeasured], func(t *testing.T) {
			var calls atomic.Int32
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				if calls.Add(1) == 1 || slowMeasured {
					select {
					case <-time.After(150 * time.Millisecond):
					case <-r.Context().Done():
						return
					}
				}
				w.WriteHeader(http.StatusNoContent)
			}))
			defer server.Close()
			duration, err := urlTestRequests(context.Background(), server.Client(), server.URL, 50*time.Millisecond, time.Second, true)
			if calls.Load() != 2 {
				t.Fatal("cold probe did not warm once and measure once")
			}
			if slowMeasured {
				if err == nil {
					t.Fatal("warmup extended the measured timeout")
				}
			} else if err != nil || duration >= 100*time.Millisecond {
				t.Fatalf("warmup included in latency: %v %v", duration, err)
			}
		})
	}
}

func TestURLWarmupCanBeCancelled(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { cancel(); <-r.Context().Done() }))
	defer server.Close()
	started := time.Now()
	_, err := urlTestRequests(ctx, server.Client(), server.URL, time.Second, time.Minute, true)
	if err == nil || time.Since(started) > time.Second {
		t.Fatal("warmup ignored cancellation")
	}
}
