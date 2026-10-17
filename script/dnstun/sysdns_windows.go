package main

import (
	"net/netip"
	"os/exec"
	"strings"
	"syscall"
	"unsafe"

	"golang.org/x/sys/windows"
)

// systemResolvers lists the DNS servers of the adapters that are up, as Windows
// itself uses them; qThrone's TUN adapter is left out.
func systemResolvers() []string {
	size := uint32(15 << 10)
	var buffer []byte
	var first *windows.IpAdapterAddresses
	for attempt := 0; attempt < 3; attempt++ {
		buffer = make([]byte, size)
		first = (*windows.IpAdapterAddresses)(unsafe.Pointer(&buffer[0]))
		err := windows.GetAdaptersAddresses(windows.AF_UNSPEC, windows.GAA_FLAG_SKIP_ANYCAST|windows.GAA_FLAG_SKIP_MULTICAST, 0, first, &size)
		if err == nil {
			break
		}
		if err != windows.ERROR_BUFFER_OVERFLOW {
			return nil
		}
		first = nil
	}
	var out []string
	seen := make(map[netip.Addr]bool)
	for adapter := first; adapter != nil; adapter = adapter.Next {
		if adapter.OperStatus != windows.IfOperStatusUp || adapter.IfType == windows.IF_TYPE_SOFTWARE_LOOPBACK {
			continue
		}
		name := strings.ToLower(windows.UTF16PtrToString(adapter.FriendlyName))
		if strings.Contains(name, "qthrone") || strings.Contains(name, "throne") {
			continue
		}
		for server := adapter.FirstDnsServerAddress; server != nil; server = server.Next {
			raw, err := server.Address.Sockaddr.Sockaddr()
			if err != nil {
				continue
			}
			switch sockaddr := raw.(type) {
			case *syscall.SockaddrInet4:
				out = appendResolver(out, seen, netip.AddrFrom4(sockaddr.Addr))
			case *syscall.SockaddrInet6:
				out = appendResolver(out, seen, netip.AddrFrom16(sockaddr.Addr))
			}
		}
	}
	return out
}

func hideChildWindow(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true, CreationFlags: windows.CREATE_NO_WINDOW}
}
