package main

import (
	"net/netip"
	"reflect"
	"testing"
	"unsafe"

	"golang.zx2c4.com/wireguard/tun/netstack"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
)

func TestTuneNetstackUsesCubicAndModeratedWindows(t *testing.T) {
	_, network, err := netstack.CreateNetTUN([]netip.Addr{netip.MustParseAddr("10.0.0.2")}, nil, 1280)
	if err != nil {
		t.Fatal(err)
	}
	tuneNetstack(network)
	s := (*stack.Stack)(unsafe.Pointer(reflect.ValueOf(network).Elem().FieldByName("stack").Pointer()))
	var cc tcpip.CongestionControlOption
	var moderate tcpip.TCPModerateReceiveBufferOption
	var receive tcpip.TCPReceiveBufferSizeRangeOption
	if s.TransportProtocolOption(tcp.ProtocolNumber, &cc) != nil || cc != "cubic" {
		t.Fatalf("congestion control %q", cc)
	}
	if s.TransportProtocolOption(tcp.ProtocolNumber, &moderate) != nil || !bool(moderate) {
		t.Fatal("receive buffer moderation is off")
	}
	if s.TransportProtocolOption(tcp.ProtocolNumber, &receive) != nil || receive.Max != 8<<20 {
		t.Fatalf("receive buffer range %+v", receive)
	}
}
