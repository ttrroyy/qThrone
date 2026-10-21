package parentcheck

import (
	"runtime"
	"testing"
)

func TestRelayHelperCannotLaunchPrivilegedCoreMode(t *testing.T) {
	gui, helper := "qThrone", "qwdtt"
	if runtime.GOOS == "windows" {
		gui += ".exe"
		helper += ".exe"
	}
	if !allowedParent("same", "same", gui, false) || !allowedParent("same", "same", helper, true) {
		t.Fatal("bundled parent rejected")
	}
	for _, tc := range []struct {
		directory, name string
		relay           bool
	}{{"same", helper, false}, {"other", helper, true}, {"same", "other", true}, {"other", gui, true}} {
		if allowedParent(tc.directory, "same", tc.name, tc.relay) {
			t.Fatal("non-bundled or privileged helper parent allowed")
		}
	}
}
