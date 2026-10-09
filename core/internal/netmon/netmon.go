package netmon

import (
	"fmt"
	"os"

	tun "github.com/sagernet/sing-tun"
	"github.com/sagernet/sing/common/control"
	"github.com/sagernet/sing/common/logger"
)

// Always-on and independent of the box lifecycle; both stay nil when the monitor could not be created.
var (
	monitor tun.DefaultInterfaceMonitor
	// Kept current by monitor on every network change.
	finder control.InterfaceFinder
)

func init() {
	// The kill switch guard is this same binary and its stdout is a line protocol.
	if len(os.Args) > 1 && os.Args[1] == "--guard" {
		return
	}
	nop := logger.NOP()
	updMonitor, err := tun.NewNetworkUpdateMonitor(nop)
	if err != nil {
		fmt.Println("Could not create NetworkUpdateMonitor")
		return
	}
	interfaceFinder := control.NewDefaultInterfaceFinder()
	defaultMonitor, err := tun.NewDefaultInterfaceMonitor(updMonitor, nop, tun.DefaultInterfaceMonitorOptions{
		InterfaceFinder: interfaceFinder,
	})
	if err != nil {
		fmt.Println("Could not create DefaultInterfaceMonitor")
		return
	}
	monitor, finder = defaultMonitor, interfaceFinder
	if err = updMonitor.Start(); err != nil {
		fmt.Println("Could not start updMonitor")
		return
	}
	if err = defaultMonitor.Start(); err != nil {
		fmt.Println("Could not start monitor")
		return
	}
}

func Monitor() tun.DefaultInterfaceMonitor {
	return monitor
}

func Finder() control.InterfaceFinder {
	return finder
}

// nil when the monitor is unavailable; TUN and loopback are excluded, so the result is safe to bind egress to while qthrone-tun is up.
func DefaultInterface() *control.Interface {
	if monitor == nil {
		return nil
	}
	return monitor.DefaultInterface()
}
