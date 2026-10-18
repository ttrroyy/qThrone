package rpc

import (
	"bufio"
	"context"
	"encoding/json"
	"io"
	"net"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"testing"
	"time"

	"qThroneCore/gen"
)

type fakeClock struct{ now time.Time }

func (c *fakeClock) time() time.Time            { return c.now }
func (c *fakeClock) advance(step time.Duration) { c.now = c.now.Add(step) }

func TestWatchdogStopsOnWrongPasswordLikeOriginal(t *testing.T) {
	clock := &fakeClock{now: time.Unix(1000, 0)}
	h := newTransportHealth(true, clock.time)
	for range 3 {
		h.accept("[GROUP] WRAP_AUTH_TIMEOUT: DTLS timeout, пароль/WRAP не подтверждён")
	}
	if h.wrongPassword() {
		t.Fatal("verdict before the original 30 s")
	}
	clock.advance(31 * time.Second)
	if !h.wrongPassword() {
		t.Fatal("three WRAP timeouts without a worker are a wrong password")
	}
	working := newTransportHealth(true, clock.time)
	working.accept("[СТАТИСТИКА] Активных: 9 | Трафик: 0.00 МБ")
	for range 3 {
		working.accept("WRAP_AUTH_TIMEOUT")
	}
	clock.advance(31 * time.Second)
	if working.wrongPassword() {
		t.Fatal("a connection that once worked is not a wrong password")
	}
}

func TestWatchdogRestartsStalledQWDTT(t *testing.T) {
	clock := &fakeClock{now: time.Unix(1000, 0)}
	h := newTransportHealth(true, clock.time)
	h.accept("[СТАТИСТИКА] Активных: 9 | Трафик: 1.00 МБ")
	clock.advance(100 * time.Second)
	if h.needsRestart() {
		t.Fatal("restart during the first two minutes")
	}
	clock.advance(30 * time.Second)
	if !h.needsRestart() {
		t.Fatal("a client silent for 90 s was not restarted")
	}
	idle := newTransportHealth(true, clock.time)
	for range 60 {
		clock.advance(3 * time.Second)
		idle.accept("[СТАТИСТИКА] Активных: 9 | Трафик: 0.00 МБ")
		if idle.needsRestart() {
			t.Fatal("restarted a healthy connection without user traffic")
		}
	}
	zero := newTransportHealth(true, clock.time)
	zero.accept("[СТАТИСТИКА] Активных: 0 | Трафик: 0.00 МБ")
	clock.advance(91 * time.Second)
	if !zero.needsRestart() {
		t.Fatal("no worker for 90 s was not restarted")
	}
	captcha := newTransportHealth(true, clock.time)
	captcha.accept("[СТАТИСТИКА] Активных: 0 | Трафик: 0.00 МБ")
	captcha.accept("[КАПЧА] Требуется решение капчи")
	clock.advance(2 * time.Minute)
	if captcha.needsRestart() {
		t.Fatal("restarted while a check was pending")
	}
	solved := newTransportHealth(true, clock.time)
	solved.accept("[СТАТИСТИКА] Активных: 0 | Трафик: 0.00 МБ")
	solved.accept("[КАПЧА] Требуется решение капчи")
	solved.accept("[STREAM 1] [КАПЧА] AUTO: ручной WebView решил капчу")
	clock.advance(91 * time.Second)
	if !solved.needsRestart() {
		t.Fatal("a solved check kept the watchdog paused")
	}
}

func TestWatchdogFollowsCSQTTEvents(t *testing.T) {
	clock := &fakeClock{now: time.Unix(1000, 0)}
	h := newTransportHealth(false, clock.time)
	h.accept("[CSQTT diagnostic] active=18 up=0 down=0")
	clock.advance(10 * time.Minute)
	if h.needsRestart() {
		t.Fatal("CSQTT reports streams on change only; silence is not a hang")
	}
	h.accept("[CSQTT diagnostic] ACTIVE_ZERO")
	clock.advance(3 * time.Second)
	if h.needsRestart() {
		t.Fatal("restart before the original 4 s")
	}
	clock.advance(2 * time.Second)
	if !h.needsRestart() {
		t.Fatal("streams below their peak were not restarted")
	}
	panel := newTransportHealth(false, clock.time)
	panel.accept("[CSQTT diagnostic] active=9 up=0 down=0")
	panel.accept("[CSQTT diagnostic] SERVER_RESTART")
	clock.advance(4 * time.Second)
	if !panel.needsRestart() {
		t.Fatal("a panel restart of the server was not followed")
	}
}

func TestOpenFluxProbeSkipsTheConnectedExit(t *testing.T) {
	active := `{"backend":"openflux","peer":"a","device_id":"x","transports":[{"type":"yandex","url":"https://disk.yandex.ru/d/1"},{"type":"direct","dial":"203.0.113.1:443"}]}`
	extraSessions = []*extraSession{{transport: true, backend: "openflux", config: active, tag: "proxy"}}
	defer func() { extraSessions = nil }()
	shared := `{"backend":"openflux","peer":"b","device_id":"y","socks":"127.0.0.1:1","transports":[{"type":"direct","dial":"203.0.113.1:443"}]}`
	if !activeOpenFluxCarrier(shared) {
		t.Fatal("a probe of the same exit would take over the connected session")
	}
	other := `{"backend":"openflux","peer":"c","device_id":"z","transports":[{"type":"mailru","url":"https://cloud.mail.ru/x"}]}`
	if activeOpenFluxCarrier(other) || activeOpenFluxCarrier(`{"backend":"olcrtc","transports":[{"type":"direct","dial":"203.0.113.1:443"}]}`) {
		t.Fatal("unrelated profile skipped")
	}
	env, err := prepareTestEnv(false, false, "", nil, "", nil, false, "", shared)
	if env != nil || err == nil || !strings.Contains(err.Error(), "skipped:") {
		t.Fatalf("shared exit was probed: %v", err)
	}
}

// TestTransportHelperProcess is a stand-in helper: it opens its listener and, on its
// first launch, exits shortly after to play a dropped transport.
func TestTransportHelperProcess(t *testing.T) {
	if len(os.Args) < 3 || os.Args[len(os.Args)-2] != "qthrone-transport-helper" {
		return
	}
	path := os.Args[len(os.Args)-1]
	data, err := os.ReadFile(path)
	if err != nil {
		os.Exit(3)
	}
	var c struct {
		SOCKS  string `json:"socks"`
		User   string `json:"socks_user"`
		Pass   string `json:"socks_pass"`
		Folder string `json:"helper_folder"`
	}
	if json.Unmarshal(data, &c) != nil {
		os.Exit(3)
	}
	counter := filepath.Join(c.Folder, "launches")
	previous, _ := os.ReadFile(counter)
	launches, _ := strconv.Atoi(string(previous))
	launches++
	_ = os.WriteFile(counter, []byte(strconv.Itoa(launches)), 0600)
	ln, err := net.Listen("tcp", c.SOCKS)
	if err != nil {
		os.Exit(4)
	}
	defer ln.Close()
	go func() {
		for {
			conn, err := ln.Accept()
			if err != nil {
				return
			}
			// The SOCKS5 login of this launch, as every bundled helper asks for it.
			go func(conn net.Conn) {
				defer conn.Close()
				head := make([]byte, 3)
				if _, err := io.ReadFull(conn, head); err != nil {
					return
				}
				conn.Write([]byte{5, 2})
				b := make([]byte, 2)
				if _, err := io.ReadFull(conn, b); err != nil {
					return
				}
				user := make([]byte, b[1])
				io.ReadFull(conn, user)
				l := make([]byte, 1)
				io.ReadFull(conn, l)
				pass := make([]byte, l[0])
				io.ReadFull(conn, pass)
				if string(user) == c.User && string(pass) == c.Pass {
					conn.Write([]byte{1, 0})
				} else {
					conn.Write([]byte{1, 1})
				}
			}(conn)
		}
	}()
	stop := make(chan struct{})
	go func() {
		scanner := bufio.NewScanner(io.LimitReader(os.Stdin, 1024))
		for scanner.Scan() {
			if scanner.Text() == "STOP" {
				break
			}
		}
		close(stop)
	}()
	if launches == 1 {
		time.Sleep(300 * time.Millisecond)
		os.Exit(1)
	}
	<-stop
	os.Exit(0)
}

func TestWatchdogReconnectsADroppedTransport(t *testing.T) {
	exe, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	folder := t.TempDir()
	name := "olcrtc"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	helper := filepath.Join(folder, name)
	data, err := os.ReadFile(exe)
	if err != nil {
		t.Fatal(err)
	}
	if err = os.WriteFile(helper, data, 0700); err != nil {
		t.Fatal(err)
	}
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	socks := ln.Addr().String()
	ln.Close()
	encoded, _ := json.Marshal(map[string]string{"backend": "olcrtc", "peer": "jitsi:room", "device_id": "watchdog-test",
		"socks": socks, "socks_user": "watchdog-user", "socks_pass": "watchdog-pass", "helper_folder": folder})
	in := &gen.LoadConfigReq{RoutedExtraProcesses: []*gen.RoutedExtraProcess{{Path: To(helper),
		Args: To("-test.run=^TestTransportHelperProcess$ qthrone-transport-helper %s"), Config: To(string(encoded)), OutboundTag: To("route-0"), NoOut: To(true)}}}
	lifecycleMu.Lock()
	err = startExtraSessions(context.Background(), in)
	lifecycleMu.Unlock()
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		lifecycleMu.Lock()
		stopExtraSessions(false)
		lifecycleMu.Unlock()
	}()
	deadline := time.Now().Add(20 * time.Second)
	for {
		count, _ := os.ReadFile(filepath.Join(folder, "launches"))
		if string(count) == "2" {
			if conn, err := net.DialTimeout("tcp", socks, 200*time.Millisecond); err == nil {
				conn.Close()
				break
			}
		}
		if time.Now().After(deadline) {
			t.Fatalf("dropped transport was not reconnected (launches %q)", count)
		}
		time.Sleep(100 * time.Millisecond)
	}
	if len(extraSessions) != 1 {
		t.Fatal("reconnect replaced the session")
	}
}
