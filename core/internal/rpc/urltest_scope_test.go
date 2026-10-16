package rpc

import (
	"context"
	"net/http"
	"net/http/httptest"
	"qThroneCore/gen"
	"sync/atomic"
	"testing"
	"time"
)

func TestURLRPCDeadlineDoesNotCancelSiblingOrNextTest(t *testing.T) {
	var calls atomic.Int32
	endpoint := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls.Add(1)
		if r.URL.Path == "/hang" {
			<-r.Context().Done()
			return
		}
		w.WriteHeader(204)
	}))
	defer endpoint.Close()
	config := `{"log":{"disabled":true},"outbounds":[{"type":"direct","tag":"proxy"}]}`
	request := func(path string) *gen.TestReq {
		return &gen.TestReq{Config: To(config), OutboundTags: []string{"proxy"}, Url: To(endpoint.URL + path), TestTimeoutMs: To(int32(5000))}
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	finished := make(chan error, 1)
	go func() {
		result, err := (&server{}).Test(ctx, request("/hang"))
		if err == nil && (len(result.Results) != 1 || result.Results[0].GetError() == "") {
			t.Error("cancelled RPC returned success")
		}
		finished <- err
	}()
	waitUntil := time.Now().Add(3 * time.Second)
	for calls.Load() == 0 {
		if time.Now().After(waitUntil) {
			t.Fatal("probe never reached HTTP endpoint")
		}
		time.Sleep(time.Millisecond)
	}
	cancel()
	for i := 0; i < 3; i++ {
		result, err := (&server{}).Test(context.Background(), request("/ok"))
		if err != nil || len(result.Results) != 1 || result.Results[0].GetError() != "" {
			t.Fatalf("sibling/next RPC failed: %v %v", result, err)
		}
	}
	select {
	case <-finished:
	case <-time.After(time.Second):
		t.Fatal("cancelled HTTP test retained its environment")
	}
}
