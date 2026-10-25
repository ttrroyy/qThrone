//go:build !darwin

package sysdns

import (
	"errors"

	tun "github.com/sagernet/sing-tun"
)

func Prepare() error {
	return errors.ErrUnsupported
}

func Apply(tun.DefaultInterfaceMonitor) error {
	return errors.ErrUnsupported
}

func Release() {}

func RestoreStale() {}
