package main

import (
	"fmt"
	"io"
	"net/netip"
	"strconv"
	"strings"
	"sync"

	"golang.zx2c4.com/wireguard/tun"
	"golang.zx2c4.com/wireguard/tun/netstack"
)

// The netstack emits and accepts IP packets. In RAW mode they go straight to
// qWDTT's dispatcher; no WireGuard device or extra OS TUN is created.
type rawPacketDevice struct {
	tun  tun.Device
	once sync.Once
	err  error
}

func (d *rawPacketDevice) Read(p []byte) (int, error) {
	sizes := []int{0}
	n, err := d.tun.Read([][]byte{p}, sizes, 0)
	if n == 0 {
		return 0, err
	}
	return sizes[0], err
}
func (d *rawPacketDevice) Write(p []byte) (int, error) {
	n, err := d.tun.Write([][]byte{p}, 0)
	if n == 0 && err == nil {
		return 0, io.ErrShortWrite
	}
	if err != nil {
		return 0, err
	}
	return len(p), nil
}
func (d *rawPacketDevice) Close() error {
	d.once.Do(func() { d.err = d.tun.Close() })
	return d.err
}

func parseRawNetConfig(conf string) ([]netip.Addr, []netip.Addr, int, error) {
	parts := strings.Split(strings.TrimPrefix(conf, "RAWCONF:"), "|")
	if !strings.HasPrefix(conf, "RAWCONF:") || len(parts) != 3 {
		return nil, nil, 0, fmt.Errorf("invalid RAWCONF")
	}
	ip, err := netip.ParseAddr(strings.Split(parts[0], "/")[0])
	if err != nil || !ip.Is4() || ip.IsUnspecified() {
		return nil, nil, 0, fmt.Errorf("invalid RAW IPv4 address")
	}
	mtu, err := strconv.Atoi(parts[2])
	if err != nil || mtu < 576 || mtu > 1500 {
		return nil, nil, 0, fmt.Errorf("invalid RAW MTU")
	}
	var dns []netip.Addr
	for _, s := range strings.Split(parts[1], ",") {
		if strings.TrimSpace(s) == "" {
			continue
		}
		a, e := netip.ParseAddr(strings.TrimSpace(s))
		if e != nil {
			return nil, nil, 0, fmt.Errorf("invalid RAW DNS address")
		}
		dns = append(dns, a)
	}
	if len(dns) == 0 {
		dns = []netip.Addr{netip.MustParseAddr("1.1.1.1")}
	}
	return []netip.Addr{ip}, dns, mtu, nil
}

func startRawNetstack(conf string) (*rawPacketDevice, *netstack.Net, error) {
	addresses, dns, mtu, err := parseRawNetConfig(conf)
	if err != nil {
		return nil, nil, err
	}
	dev, network, err := netstack.CreateNetTUN(addresses, dns, mtu)
	if err != nil {
		return nil, nil, err
	}
	return &rawPacketDevice{tun: dev}, network, nil
}
