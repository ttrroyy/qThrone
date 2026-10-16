//go:build windows

package process

import (
	"fmt"
	"os"
	"strings"
	"sync"
	"testing"
	"time"

	"golang.org/x/sys/windows"
)

func TestAdministrativeSIDClassification(t *testing.T) {
	if isAdminSid(nil) {
		t.Fatal("nil SID classified as administrative")
	}
	for _, test := range []struct {
		sid   string
		admin bool
	}{
		{"S-1-5-32-544", true},
		{"S-1-5-21-100-200-300-512", true},
		{"S-1-5-21-100-200-300-518", true},
		{"S-1-5-21-100-200-300-519", true},
		{"S-1-5-21-100-200-300-520", true},
		{"S-1-5-21-100-200-300-513", false},
		{"S-1-5-32-545", false},
		{"S-1-5-18", false},
	} {
		sid, err := windows.StringToSid(test.sid)
		if err != nil {
			t.Fatal(err)
		}
		if got := isAdminSid(sid); got != test.admin {
			t.Errorf("SID %s: admin=%v, want %v", test.sid, got, test.admin)
		}
	}
}

func TestRestrictedTokenChildHelper(t *testing.T) {
	if len(os.Args) < 2 || os.Args[len(os.Args)-1] != "qthrone-restricted-child" {
		return
	}
	self, err := selfToken()
	if err != nil {
		t.Fatal(err)
	}
	defer self.Close()
	if self.IsElevated() {
		t.Fatal("helper retained elevated token")
	}
	fmt.Println("restricted-child-ready")
}

func TestRestrictedSelfUsesNativeProcessCreation(t *testing.T) {
	self, err := selfToken()
	if err != nil {
		t.Fatal(err)
	}
	defer self.Close()
	if !self.IsElevated() {
		t.Skip("restricted-self production path requires an elevated parent; no elevation is requested by tests")
	}
	token, err := restrictedSelfToken(self)
	if err != nil {
		t.Fatal(err)
	}
	defer token.Close()
	exe, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	var mu sync.Mutex
	var output strings.Builder
	child, err := startAsUser(exe, []string{"-test.run=^TestRestrictedTokenChildHelper$", "qthrone-restricted-child"}, true, token, nil, func(b []byte) { mu.Lock(); output.Write(b); mu.Unlock() })
	if err != nil {
		t.Fatal(err)
	}
	done := make(chan error, 1)
	go func() { done <- child.Wait() }()
	select {
	case err := <-done:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(10 * time.Second):
		child.Kill()
		t.Fatal("restricted child did not initialize within startup deadline")
	}
	mu.Lock()
	defer mu.Unlock()
	if !strings.Contains(output.String(), "restricted-child-ready") {
		t.Fatalf("restricted child did not initialize: %s", output.String())
	}
}
