package main

import (
	"context"
	"fmt"
	"net"
	"sync"

	"gvisor.dev/gvisor/pkg/buffer"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/adapters/gonet"
	"gvisor.dev/gvisor/pkg/tcpip/header"
	"gvisor.dev/gvisor/pkg/tcpip/link/channel"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
	"gvisor.dev/gvisor/pkg/tcpip/transport/tcp"
	"gvisor.dev/gvisor/pkg/tcpip/transport/udp"

	"swiftsstp/tunnel"
)

const nicID = tcpip.NICID(1)

type virtualNet struct {
	stack   *stack.Stack
	link    *channel.Endpoint
	localIP net.IP
	proto   tcpip.NetworkProtocolNumber
	dns     []net.IP
	ipv4OK  bool
	ipv6OK  bool

	cancel context.CancelFunc
	wg     sync.WaitGroup
}

type PacketStream interface {
	ReadIP(buf []byte) (int, error)
	WriteIP(pkt []byte) error
}

func newVirtualNet(sess PacketStream, res tunnel.Result, dns []net.IP, ipv4OK, ipv6OK bool, logf func(string, ...any)) (*virtualNet, error) {
	mtu := res.MTU
	if mtu <= 0 {
		mtu = tunnel.DefaultMTU
	}
	link := channel.New(512, uint32(mtu), "")
	s := stack.New(stack.Options{
		NetworkProtocols: []stack.NetworkProtocolFactory{
			ipv4.NewProtocol,
			ipv6.NewProtocol,
		},
		TransportProtocols: []stack.TransportProtocolFactory{
			tcp.NewProtocol,
			udp.NewProtocol,
		},
	})
	if err := s.CreateNIC(nicID, link); err != nil {
		return nil, fmt.Errorf("netstack: CreateNIC: %s", err)
	}

	vn := &virtualNet{
		stack:   s,
		link:    link,
		localIP: res.LocalIP,
		dns:     dns,
	}

	local := res.LocalIP
	var routes []tcpip.Route
	switch {
	case local.To4() != nil && ipv4OK:
		a := tcpip.AddrFrom4Slice(local.To4())
		vn.proto = ipv4.ProtocolNumber
		vn.ipv4OK = true
		s.AddProtocolAddress(nicID, tcpip.ProtocolAddress{
			Protocol:          ipv4.ProtocolNumber,
			AddressWithPrefix: tcpip.AddressWithPrefix{Address: a, PrefixLen: 32},
		}, stack.AddressProperties{})
		routes = append(routes, tcpip.Route{Destination: header.IPv4EmptySubnet, NIC: nicID, SourceHint: a})
	default:
		vn.proto = ipv6.ProtocolNumber
		vn.ipv6OK = true
		var b [16]byte
		copy(b[:], local.To16())
		a := tcpip.AddrFrom16(b)
		s.AddProtocolAddress(nicID, tcpip.ProtocolAddress{
			Protocol:          ipv6.ProtocolNumber,
			AddressWithPrefix: tcpip.AddressWithPrefix{Address: a, PrefixLen: 128},
		}, stack.AddressProperties{})
		routes = append(routes, tcpip.Route{Destination: header.IPv6EmptySubnet, NIC: nicID, SourceHint: a})
	}
	s.SetRouteTable(routes)

	tcpFwd := tcp.NewForwarder(s, 0, 64, func(r *tcp.ForwarderRequest) {
		r.Complete(true)
	})
	s.SetTransportProtocolHandler(tcp.ProtocolNumber, tcpFwd.HandlePacket)

	udpFwd := udp.NewForwarder(s, func(*udp.ForwarderRequest) bool { return false })
	s.SetTransportProtocolHandler(udp.ProtocolNumber, udpFwd.HandlePacket)

	ctx, cancel := context.WithCancel(context.Background())
	vn.cancel = cancel

	// Outbound: netstack -> tunnel
	vn.wg.Add(2)
	go func() {
		defer vn.wg.Done()
		for {
			pkt := link.ReadContext(ctx)
			if pkt == nil {
				return
			}
			v := pkt.ToView()
			pkt.DecRef()
			data := v.AsSlice()
			if err := sess.WriteIP(data); err != nil {
				logf("netstack: write to tunnel: %v", err)
				return
			}
		}
	}()

	// Inbound: tunnel -> netstack
	go func() {
		defer vn.wg.Done()
		buf := make([]byte, 65535)
		for {
			n, err := sess.ReadIP(buf)
			if err != nil {
				return
			}
			if n == 0 {
				continue
			}
			p := ipv4.ProtocolNumber
			if buf[0]>>4 == 6 {
				p = ipv6.ProtocolNumber
			}
			pkt := stack.NewPacketBuffer(stack.PacketBufferOptions{
				Payload: buffer.MakeWithData(buf[:n]),
			})
			link.InjectInbound(p, pkt)
			pkt.DecRef()
		}
	}()

	return vn, nil
}

func (vn *virtualNet) close() {
	if vn == nil {
		return
	}
	vn.cancel()
	vn.stack.Close()
	vn.link.Close()
	vn.wg.Wait()
}

func (vn *virtualNet) netProtoFor(ip net.IP) (tcpip.NetworkProtocolNumber, tcpip.Address, error) {
	if ip.To4() != nil && !vn.ipv4OK {
		return 0, tcpip.Address{}, fmt.Errorf("IPv4 disabled by profile")
	}
	if ip.To4() == nil && !vn.ipv6OK {
		return 0, tcpip.Address{}, fmt.Errorf("IPv6 disabled by profile")
	}
	if ip.To4() != nil {
		return ipv4.ProtocolNumber, tcpip.AddrFrom4Slice(ip.To4()), nil
	}
	var b [16]byte
	copy(b[:], ip.To16())
	return ipv6.ProtocolNumber, tcpip.AddrFrom16(b), nil
}

func (vn *virtualNet) dialTCP(ctx context.Context, ip net.IP, port int) (net.Conn, error) {
	proto, addr, err := vn.netProtoFor(ip)
	if err != nil {
		return nil, err
	}
	return gonet.DialContextTCP(ctx, vn.stack, tcpip.FullAddress{NIC: nicID, Addr: addr, Port: uint16(port)}, proto)
}

func (vn *virtualNet) dialUDP(ip net.IP, port int) (*gonet.UDPConn, error) {
	proto, addr, err := vn.netProtoFor(ip)
	if err != nil {
		return nil, err
	}
	return gonet.DialUDP(vn.stack, nil, &tcpip.FullAddress{NIC: nicID, Addr: addr, Port: uint16(port)}, proto)
}
