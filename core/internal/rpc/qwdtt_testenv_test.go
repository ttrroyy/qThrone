package rpc

import (
	"context"
	"testing"
)

func TestQWDTTProbeRejectsNonLoopbackListeners(t *testing.T) {
	for _, config := range []string{`{}`, `not-json`, `{"socks":"0.0.0.0:9000"}`, `{"socks":"example.invalid:9000"}`, `{"socks":"127.0.0.1:0"}`} {
		cleanup, err := prepareQWDTTProbe(context.Background(), config)
		if cleanup != nil {
			cleanup()
		}
		if err == nil {
			t.Fatal("invalid test bridge listener was accepted")
		}
	}
}

func TestQWDTTProbeWaitingCanBeCancelled(t *testing.T) {
	qwdttProbeSlot <- struct{}{}
	defer func() { <-qwdttProbeSlot }()
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	cleanup, err := prepareQWDTTProbe(ctx, `{"socks":"127.0.0.1:19000"}`)
	if cleanup != nil {
		cleanup()
	}
	if err != context.Canceled {
		t.Fatalf("queued test did not observe cancellation: %v", err)
	}
}
