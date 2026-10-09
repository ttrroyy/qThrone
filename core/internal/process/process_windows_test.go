//go:build windows

package process

import (
	"testing"

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
