package ssr

import (
	"context"
	"errors"
	"fmt"
	"net"
	"strings"

	MN "github.com/metacubex/mihomo/common/net"
	"github.com/metacubex/mihomo/transport/shadowsocks/core"
	"github.com/metacubex/mihomo/transport/shadowsocks/shadowstream"
	"github.com/metacubex/mihomo/transport/socks5"
	"github.com/metacubex/mihomo/transport/ssr/obfs"
	"github.com/metacubex/mihomo/transport/ssr/protocol"
	"github.com/sagernet/sing-box/adapter"
	"github.com/sagernet/sing-box/adapter/outbound"
	"github.com/sagernet/sing-box/common/dialer"
	"github.com/sagernet/sing-box/log"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing/common/bufio"
	M "github.com/sagernet/sing/common/metadata"
	N "github.com/sagernet/sing/common/network"
)

const Type = "shadowsocksr"

type Options struct {
	option.DialerOptions
	option.ServerOptions
	Method        string             `json:"method"`
	Password      string             `json:"password"`
	Protocol      string             `json:"protocol,omitempty"`
	ProtocolParam string             `json:"protocol_param,omitempty"`
	Obfs          string             `json:"obfs,omitempty"`
	ObfsParam     string             `json:"obfs_param,omitempty"`
	Network       option.NetworkList `json:"network,omitempty"`
}

func Register(registry *outbound.Registry) {
	outbound.Register[Options](registry, Type, NewOutbound)
}

type Outbound struct {
	outbound.Adapter
	dialer   N.Dialer
	server   M.Socksaddr
	cipher   core.Cipher
	obfs     obfs.Obfs
	protocol protocol.Protocol
}

func NewOutbound(ctx context.Context, _ adapter.Router, _ log.ContextLogger, tag string, options Options) (adapter.Outbound, error) {
	if options.Server == "" || strings.ContainsAny(options.Server, " /\t\r\n") || options.ServerPort == 0 {
		return nil, errors.New("SSR: invalid server address or port")
	}
	method := strings.ToLower(strings.TrimSpace(options.Method))
	if method != "none" && options.Password == "" {
		return nil, errors.New("SSR: password is required")
	}
	if method == "none" {
		method = "dummy"
	}
	cipher, err := core.PickCipher(method, nil, options.Password)
	if err != nil {
		return nil, fmt.Errorf("SSR cipher: %w", err)
	}
	var key []byte
	var ivSize int
	if method == "dummy" {
		key = core.Kdf(options.Password, 16)
	} else if stream, ok := cipher.(*core.StreamCipher); ok {
		key, ivSize = stream.Key, stream.IVSize()
	} else {
		return nil, errors.New("SSR needs a stream cipher or none")
	}
	if options.Obfs == "" {
		options.Obfs = "plain"
	}
	if options.Protocol == "" {
		options.Protocol = "origin"
	}
	obfuscator, overhead, err := obfs.PickObfs(options.Obfs, &obfs.Base{
		Host: options.Server, Port: int(options.ServerPort), Key: key, IVSize: ivSize, Param: options.ObfsParam,
	})
	if err != nil {
		return nil, err
	}
	transport, err := protocol.PickProtocol(options.Protocol, &protocol.Base{Key: key, Overhead: overhead, Param: options.ProtocolParam})
	if err != nil {
		return nil, err
	}
	upstream, err := dialer.New(ctx, options.DialerOptions, options.ServerIsDomain())
	if err != nil {
		return nil, err
	}
	return &Outbound{
		Adapter: outbound.NewAdapterWithDialerOptions(Type, tag, options.Network.Build(), options.DialerOptions),
		dialer:  upstream, server: options.ServerOptions.Build(), cipher: cipher, obfs: obfuscator, protocol: transport,
	}, nil
}

func (h *Outbound) DialContext(ctx context.Context, network string, destination M.Socksaddr) (_ net.Conn, err error) {
	if N.NetworkName(network) == N.NetworkUDP {
		packet, err := h.ListenPacket(ctx, destination)
		if err != nil {
			return nil, err
		}
		return bufio.NewBindPacketConn(packet, destination), nil
	}
	if N.NetworkName(network) != N.NetworkTCP {
		return nil, fmt.Errorf("SSR: unknown network %s", network)
	}
	ctx, metadata := adapter.ExtendContext(ctx)
	metadata.Outbound, metadata.Destination = h.Tag(), destination
	raw, err := h.dialer.DialContext(ctx, N.NetworkTCP, h.server)
	if err != nil {
		return nil, err
	}
	defer func() {
		if err != nil {
			raw.Close()
		}
	}()
	if ctx.Done() != nil {
		done := MN.SetupContextForConn(ctx, raw)
		defer done(&err)
	}
	conn := h.cipher.StreamConn(h.obfs.StreamConn(raw))
	var iv []byte
	if stream, ok := conn.(*shadowstream.Conn); ok {
		iv, err = stream.ObtainWriteIV()
		if err != nil {
			return nil, err
		}
	}
	conn = h.protocol.StreamConn(conn, iv)
	if err = M.SocksaddrSerializer.WriteAddrPort(conn, destination); err != nil {
		return nil, err
	}
	return conn, nil
}

func (h *Outbound) ListenPacket(ctx context.Context, destination M.Socksaddr) (net.PacketConn, error) {
	ctx, metadata := adapter.ExtendContext(ctx)
	metadata.Outbound, metadata.Destination = h.Tag(), destination
	raw, err := h.dialer.DialContext(ctx, N.NetworkUDP, h.server)
	if err != nil {
		return nil, err
	}
	packet := h.cipher.PacketConn(MN.NewEnhancePacketConn(bufio.NewUnbindPacketConn(raw)))
	return &packetConn{PacketConn: h.protocol.PacketConn(packet), server: raw.RemoteAddr()}, nil
}

type packetConn struct {
	net.PacketConn
	server net.Addr
}

func (c *packetConn) WriteTo(data []byte, addr net.Addr) (int, error) {
	packet, err := socks5.EncodeUDPPacket(socks5.ParseAddrToSocksAddr(addr), data)
	if err != nil {
		return 0, err
	}
	if _, err = c.PacketConn.WriteTo(packet[3:], c.server); err != nil {
		return 0, err
	}
	return len(data), nil
}

func (c *packetConn) ReadFrom(data []byte) (int, net.Addr, error) {
	n, _, err := c.PacketConn.ReadFrom(data)
	if err != nil {
		return 0, nil, err
	}
	address := socks5.SplitAddr(data[:n])
	if address == nil {
		return 0, nil, errors.New("SSR: invalid UDP destination")
	}
	destination := M.ParseSocksaddr(address.String())
	copy(data, data[len(address):n])
	return n - len(address), destination, nil
}
