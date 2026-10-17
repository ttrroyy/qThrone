package main

import (
	"bufio"
	"net/netip"
	"os"
	"strings"
)

// usableResolver drops what a DNS tunnel cannot use as its resolver: loopback
// stubs, link-local addresses and the private ranges qThrone gives its own TUN
// (whose DNS answers only through the tunnel being started).
func usableResolver(address netip.Addr) bool {
	address = address.Unmap()
	if !address.IsValid() || address.IsLoopback() || address.IsUnspecified() || address.IsLinkLocalUnicast() || address.IsMulticast() {
		return false
	}
	for _, prefix := range []string{"172.19.0.0/16", "fdfe:dcba:9876::/48"} {
		if netip.MustParsePrefix(prefix).Contains(address) {
			return false
		}
	}
	return true
}

func appendResolver(out []string, seen map[netip.Addr]bool, address netip.Addr) []string {
	address = address.Unmap()
	if !usableResolver(address) || seen[address] {
		return out
	}
	seen[address] = true
	return append(out, netip.AddrPortFrom(address, 53).String())
}

// resolvConfServers reads the nameservers of a resolv.conf.
func resolvConfServers(path string) []string {
	file, err := os.Open(path)
	if err != nil {
		return nil
	}
	defer file.Close()
	var out []string
	seen := make(map[netip.Addr]bool)
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		fields := strings.Fields(scanner.Text())
		if len(fields) < 2 || fields[0] != "nameserver" {
			continue
		}
		if address, err := netip.ParseAddr(strings.SplitN(fields[1], "%", 2)[0]); err == nil {
			out = appendResolver(out, seen, address)
		}
	}
	return out
}
