package rpc

import (
	"context"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"sync/atomic"
	"testing"
	"time"

	"ThroneCore/gen"
	"ThroneCore/internal/boxmain"
	"ThroneCore/internal/process"
)

func TestRoutedTunnelDuplicateDeviceRejectedBeforeStart(t *testing.T) {
	conf := `{"backend":"csqtt","peer":"example.invalid:46000","device_id":"test-device"}`
	in := &gen.LoadConfigReq{RoutedExtraProcesses: []*gen.RoutedExtraProcess{
		{Path: To("qwdtt.exe"), Config: To(conf), OutboundTag: To("route-0")},
		{Path: To("qwdtt.exe"), Config: To(conf), OutboundTag: To("route-1")},
	}}
	if err := startExtraSessions(context.Background(), in); err == nil {
		t.Fatal("duplicate device accepted")
	}
	if len(extraSessions) != 0 {
		t.Fatal("validation started a process")
	}
}

func TestRoutedTunnelLookupAndTeardown(t *testing.T) {
	conf := `{"backend":"csqtt","peer":"example.invalid:46000","device_id":"test-device"}`
	key, _ := qwdttSessionKey(conf)
	released := false
	extraSessions = []*extraSession{{child: process.NewProcess("qwdtt.exe", nil, true), transport: true,
		key: key, tag: "route-2", release: func() { released = true }},
		{child: process.NewProcess("other.exe", nil, true)}}
	defer stopExtraSessions(false)
	if tag, ok := activeTransportTag(conf); !ok || tag != "route-2" {
		t.Fatal("routed transport not found")
	}
	if _, ok := activeTransportTag(`{"peer":"other.invalid:46000","device_id":"test-device"}`); ok {
		t.Fatal("unrelated session reused")
	}
	stopExtraSessions(true)
	if !released || len(extraSessions) != 1 {
		t.Fatal("tunnel stop leaked gate or stopped generic extra process")
	}
	if _, ok := activeTransportTag(conf); ok {
		t.Fatal("stopped transport remains active")
	}
}

func TestExtraStartFailureRollsBackAndReleasesDevice(t *testing.T) {
	// First entry cannot run: its private configuration and device reservation
	// must still be removed when the OS rejects the executable.
	dir := t.TempDir()
	path := filepath.Join(dir, "qwdtt.exe")
	conf := `{"peer":"example.invalid:56000","device_id":"rollback-device"}`
	in := &gen.LoadConfigReq{RoutedExtraProcesses: []*gen.RoutedExtraProcess{
		{Path: To(path), Args: To("-config %s"), Config: To(conf), OutboundTag: To("route-0")},
	}}
	if err := startExtraSessions(context.Background(), in); err == nil {
		t.Fatal("missing process started")
	}
	if len(extraSessions) != 0 {
		t.Fatal("failed startup leaked session")
	}
	key, _ := qwdttSessionKey(conf)
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	release, err := acquireQWDTTProbe(ctx, key)
	if err != nil {
		t.Fatal("failed startup leaked device reservation")
	}
	release()
	if entries, _ := os.ReadDir(dir); len(entries) != 0 {
		t.Fatal("unexpected test artifacts")
	}
}

func TestRoutedProbeReportsStandaloneTag(t *testing.T) {
	env := &testEnv{reportTag: "proxy", live: true}
	if env.resultTag("route-4") != "proxy" {
		t.Fatal("UI cannot match the profile result")
	}
	if (&testEnv{}).resultTag("ordinary") != "ordinary" {
		t.Fatal("ordinary result changed")
	}
}

func TestRoutedURLProbeUsesLiveOutboundWithoutRestart(t *testing.T) {
	var calls atomic.Int32
	endpoint := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		calls.Add(1)
		w.WriteHeader(http.StatusNoContent)
	}))
	defer endpoint.Close()
	// The default proxy cannot pass traffic: success proves we selected the
	// specific routed transport, not the main profile or the default route.
	box, cancel, err := boxmain.Create([]byte(`{"log":{"disabled":true},"outbounds":[{"type":"block","tag":"proxy"},{"type":"direct","tag":"route-transport"}],"route":{"final":"proxy"}}`), nil)
	if err != nil {
		t.Fatal(err)
	}
	setBoxInstance(box, cancel)
	defer func() {
		setBoxInstance(nil, nil)
		box.CloseWithTimeout(cancel, time.Second, func(...any) {}, false)
		extraSessions = nil
	}()
	conf := `{"backend":"csqtt","peer":"example.invalid:46000","device_id":"live-device"}`
	key, _ := qwdttSessionKey(conf)
	session := &extraSession{transport: true, key: key, tag: "route-transport"}
	extraSessions = []*extraSession{session}
	result, err := (&server{}).Test(context.Background(), &gen.TestReq{
		QwdttConfig: To(conf), OutboundTags: []string{"proxy"}, Url: To(endpoint.URL), TestTimeoutMs: To(int32(1000)),
	})
	if err != nil || len(result.GetResults()) != 1 || result.Results[0].GetError() != "" {
		t.Fatalf("live routed probe failed: %v %v", result, err)
	}
	if result.Results[0].GetOutboundTag() != "proxy" || calls.Load() != 2 {
		t.Fatal("live routed test was not warmed and reported against its profile")
	}
	if currentBox() != box || len(extraSessions) != 1 || extraSessions[0] != session {
		t.Fatal("testing replaced or stopped the active connection")
	}
}
