package rpc

import (
	"context"
	"crypto/sha256"
	"testing"
	"time"
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
	key := sha256.Sum256([]byte("same-device"))
	release, err := acquireQWDTTProbe(context.Background(), key)
	if err != nil {
		t.Fatal(err)
	}
	defer release()
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	cleanup, err := acquireQWDTTProbe(ctx, key)
	if cleanup != nil {
		cleanup()
	}
	if err != context.Canceled {
		t.Fatalf("queued test did not observe cancellation: %v", err)
	}
}

func TestQWDTTProbeDevicesRunIndependently(t *testing.T) {
	key := sha256.Sum256([]byte("server-a/device-a"))
	release, err := acquireQWDTTProbe(context.Background(), key)
	if err != nil {
		t.Fatal(err)
	}
	defer release()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	other, err := acquireQWDTTProbe(ctx, sha256.Sum256([]byte("server-b/device-b")))
	if err != nil {
		t.Fatalf("independent probe was blocked: %v", err)
	}
	other()
	ctx, cancel = context.WithTimeout(context.Background(), 30*time.Millisecond)
	defer cancel()
	if duplicate, err := acquireQWDTTProbe(ctx, key); err != context.DeadlineExceeded {
		if duplicate != nil {
			duplicate()
		}
		t.Fatalf("duplicate device was not serialized: %v", err)
	}
	release()
	release() // cleanup is safe if called again during cancellation.
	qwdttProbeMu.Lock()
	defer qwdttProbeMu.Unlock()
	if len(qwdttProbeGates) != 0 {
		t.Fatal("finished probes left gates behind")
	}
}

func TestQWDTTSessionIdentity(t *testing.T) {
	first, ok := qwdttSessionKey(`{"peer":"server-a:56003","device_id":"device-a","socks":"127.0.0.1:19000"}`)
	if !ok {
		t.Fatal("valid session was rejected")
	}
	same, ok := qwdttSessionKey(`{"peer":"server-a:56003","device_id":"device-a","socks":"127.0.0.1:19001"}`)
	if !ok || same != first {
		t.Fatal("local probe port changed the server session identity")
	}
	for _, config := range []string{
		`{"peer":"server-b:56003","device_id":"device-a"}`,
		`{"peer":"server-a:56003","device_id":"device-b"}`,
	} {
		other, ok := qwdttSessionKey(config)
		if !ok || other == first {
			t.Fatal("independent session shares a probe gate")
		}
	}
	for _, config := range []string{`{}`, `{"peer":"server-a:56003"}`, `invalid`} {
		if _, ok := qwdttSessionKey(config); ok {
			t.Fatal("missing session identity was accepted")
		}
	}
}
