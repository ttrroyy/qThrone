package rpc

import (
	"context"
	"crypto/sha256"
	"fmt"
	"os"
	"testing"
	"time"
)

func TestQWDTTPingWaitsForItsDeviceUntilCancelled(t *testing.T) {
	config := `{"peer":"example.invalid:56000","device_id":"ping-budget-test"}`
	key, valid := qwdttSessionKey(config)
	if !valid {
		t.Fatal("invalid fixture")
	}
	pingKey := sha256.Sum256(append([]byte("qwdtt native ping\x00"), key[:]...))
	release, err := acquireQWDTTProbe(context.Background(), pingKey)
	if err != nil {
		t.Fatal(err)
	}
	defer release()
	// A short HTTP timeout no longer fails a ping queued behind its device's previous one.
	ctx, cancel := context.WithTimeout(context.Background(), 300*time.Millisecond)
	defer cancel()
	start := time.Now()
	_, err = runQWDTTPing(ctx, config, 50*time.Millisecond)
	if err == nil || time.Since(start) < 250*time.Millisecond || time.Since(start) > 2*time.Second {
		t.Fatalf("queued ping did not wait for its device or ignored cancellation: %v", err)
	}
}

func TestQWDTTDeviceSessionsAreKeyedByServer(t *testing.T) {
	raw, _ := qwdttSessionKey(`{"peer":"Server.example:56003","device_id":"device"}`)
	wg, _ := qwdttSessionKey(`{"peer":"server.example:56000","device_id":"device"}`)
	other, _ := qwdttSessionKey(`{"peer":"server.example:56000","device_id":"other"}`)
	csqtt, _ := qwdttSessionKey(`{"backend":"csqtt","peer":"server.example:46000","device_id":"device"}`)
	if raw != wg || raw == other || raw == csqtt {
		t.Fatal("device sessions are not keyed by server and device")
	}
}

func TestCSQTTLegacyPasswordOwnsSessionAcrossDevices(t *testing.T) {
	first, ok := qwdttSessionKey(`{"backend":"csqtt","peer":"Server.example:46000","device_id":"first","password":"shared"}`)
	same, valid := qwdttSessionKey(`{"backend":"csqtt","peer":"server.example:46000","device_id":"other","password":"shared"}`)
	other, _ := qwdttSessionKey(`{"backend":"csqtt","peer":"server.example:46000","device_id":"first","password":"another"}`)
	if !ok || !valid || first != same || first == other {
		t.Fatal("Legacy probe would replace an active password session")
	}
}

func TestQWDTTPingBackendSelection(t *testing.T) {
	for _, c := range []string{`{"peer":"server:1"}`, `{"backend":""}`} {
		if !nativeQWDTTPing(c) {
			t.Fatal("qWDTT was not selected")
		}
	}
	for _, c := range []string{"", "null", "invalid", `{"backend":"csqtt"}`, `{"backend":"openflux"}`, `{"backend":"olcrtc"}`} {
		if nativeQWDTTPing(c) {
			t.Fatalf("unexpected native ping: %s", c)
		}
	}
}

func TestQWDTTPingOutput(t *testing.T) {
	for input, want := range map[string]string{"PING_ERROR|VK authorization: private-token\n": "VK authorization failed", "PING_ERROR|TURN/DTLS: private-endpoint\n": "TURN/DTLS server probe failed"} {
		var failure qwdttPingOutput
		failure.write([]byte(input))
		if !failure.failed || failure.failureReason != want {
			t.Fatal("ping failure stage not sanitized")
		}
	}
	var p qwdttPingOutput
	for _, chunk := range []string{"startup\nPING_RES", "ULT|12", "3\r\nshutdown\n"} {
		p.write([]byte(chunk))
	}
	if !p.found || p.delay != 123 || p.failed {
		t.Fatal("split result not parsed")
	}
	for _, bad := range []string{"-1", "2147483648", "abc"} {
		var p qwdttPingOutput
		p.write([]byte("PING_RESULT|" + bad + "\n"))
		if p.found || !p.failed {
			t.Fatal("invalid result accepted")
		}
	}
}

func TestQWDTTPingChild(t *testing.T) {
	for i, a := range os.Args {
		if a != "--native-ping-helper" || i+1 >= len(os.Args) {
			continue
		}
		switch os.Args[i+1] {
		case "success":
			fmt.Print("PING_RES")
			fmt.Print("ULT|57\n")
			os.Exit(0)
		case "error":
			fmt.Print("PING_ERROR|private upstream error\n")
			os.Exit(1)
		case "missing":
			fmt.Println("no result")
			os.Exit(0)
		case "wait":
			time.Sleep(time.Minute)
			os.Exit(0)
		}
	}
}

func TestQWDTTPingProcessResultAndCancellation(t *testing.T) {
	executable, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	for _, mode := range []string{"success", "error", "missing", "wait"} {
		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		if mode == "wait" {
			cancel()
			ctx, cancel = context.WithTimeout(context.Background(), 200*time.Millisecond)
		}
		delay, err := executeQWDTTPing(ctx, executable, []string{"-test.run=^TestQWDTTPingChild$", "--", "--native-ping-helper", mode})
		cancel()
		if mode == "success" {
			if err != nil || delay != 57 {
				t.Fatalf("result: %d %v", delay, err)
			}
		} else if err == nil {
			t.Fatalf("%s accepted", mode)
		}
	}
}
