//go:build unix

package guard

import (
	"os"
	"syscall"
)

func IdentityEnabled() bool {
	return os.Getenv(EnvIdentity) == "1" || os.Getenv("THRONE_GUARD") == "1"
}

// ApplyIdentity must run before the core opens any socket: the kernel stamps a socket with the creator's fsgid once.
func ApplyIdentity() error {
	if !IdentityEnabled() || os.Geteuid() != 0 {
		return nil
	}
	return syscall.Setegid(GID)
}
