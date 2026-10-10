package process

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestOutputObserverHelper(t *testing.T) {
	if os.Args[len(os.Args)-1] == "qthrone-output-observer-helper" {
		fmt.Println("[CSQTT] CAPTCHA_REQUIRED")
		os.Exit(0)
	}
}

func TestOutputObserverReceivesMutedChildOutput(t *testing.T) {
	exe, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	observed := make(chan struct{}, 1)
	p := NewProcess(exe, []string{"-test.run=^TestOutputObserverHelper$", "qthrone-output-observer-helper"}, true)
	p.SetBackgroundProbe()
	p.SetOutputObserver(func(b []byte) {
		if strings.Contains(string(b), "CAPTCHA_REQUIRED") {
			select {
			case observed <- struct{}{}:
			default:
			}
		}
	})
	if err := p.Start(); err != nil {
		t.Fatal(err)
	}
	defer p.Stop()
	select {
	case <-observed:
	case <-time.After(5 * time.Second):
		t.Fatal("child progress was not observed")
	}
}

func TestFailedProbeHelper(t *testing.T) {
	if len(os.Args) > 1 && os.Args[len(os.Args)-1] == "qthrone-failed-probe-helper" {
		os.Exit(1)
	}
}

func TestFailedProbeDoesNotSignalActiveProfileCrash(t *testing.T) {
	exe, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	for _, probe := range []bool{false, true} {
		r, w, err := os.Pipe()
		if err != nil {
			t.Fatal(err)
		}
		previous := os.Stdout
		os.Stdout = w
		p := NewProcess(exe, []string{"-test.run=^TestFailedProbeHelper$", "qthrone-failed-probe-helper"}, true)
		if probe {
			p.SetBackgroundProbe()
		}
		err = p.Start()
		if err == nil {
			select {
			case <-p.Done():
			case <-time.After(5 * time.Second):
				p.Stop()
				err = os.ErrDeadlineExceeded
			}
		}
		os.Stdout = previous
		_ = w.Close()
		output, readErr := io.ReadAll(r)
		_ = r.Close()
		if err != nil || readErr != nil {
			t.Fatalf("child exit: %v %v", err, readErr)
		}
		crash := strings.Contains(string(output), "Extra process exited unexpectedly")
		if crash == probe {
			t.Fatalf("probe=%v emitted wrong crash signal: %s", probe, output)
		}
		if probe && !strings.Contains(string(output), "Background probe process exited") {
			t.Fatalf("probe failure was not logged: %s", output)
		}
	}
}

func TestStdinShutdownHelper(t *testing.T) {
	if len(os.Args) < 3 || os.Args[len(os.Args)-2] != "qthrone-stdin-helper" {
		return
	}
	scanner := bufio.NewScanner(os.Stdin)
	if scanner.Scan() {
		_ = os.WriteFile(os.Args[len(os.Args)-1], []byte(scanner.Text()), 0600)
		os.Exit(0)
	}
	time.Sleep(time.Minute)
	os.Exit(1)
}

func TestOptionalGracefulShutdown(t *testing.T) {
	exe, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	result := filepath.Join(t.TempDir(), "result")
	p := NewProcess(exe, []string{"-test.run=^TestStdinShutdownHelper$", "qthrone-stdin-helper", result}, true)
	p.EnableStdinShutdown("STOP")
	if err = p.Start(); err != nil {
		t.Fatal(err)
	}
	p.Stop()
	select {
	case <-p.Done():
	case <-time.After(5 * time.Second):
		t.Fatal("child did not stop")
	}
	got, err := os.ReadFile(result)
	if err != nil || string(got) != "STOP" {
		t.Fatalf("graceful command was not delivered: %q %v", got, err)
	}
}

func TestOrdinaryExtraProcessStillStops(t *testing.T) {
	exe, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	result := filepath.Join(t.TempDir(), "result")
	p := NewProcess(exe, []string{"-test.run=^TestStdinShutdownHelper$", "qthrone-stdin-helper", result}, true)
	if err = p.Start(); err != nil {
		t.Fatal(err)
	}
	p.Stop()
	select {
	case <-p.Done():
	case <-time.After(5 * time.Second):
		t.Fatal("ordinary extra process was not killed")
	}
	if _, err = os.Stat(result); !os.IsNotExist(err) {
		t.Fatal("ordinary process received a shutdown command")
	}
}
