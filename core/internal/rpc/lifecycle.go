package rpc

import (
	"context"
	"fmt"
	"log"
	"net/netip"
	"runtime"
	"time"

	"ThroneCore/gen"
	"ThroneCore/internal/boxmain"
	"ThroneCore/internal/sysdns"
	"ThroneCore/internal/xray"
)

func (s *server) Start(ctx context.Context, in *gen.LoadConfigReq) (out *gen.ErrorResp, _ error) {
	lifecycleMu.Lock()
	defer lifecycleMu.Unlock()

	var err error
	if currentBox() != nil {
		return &gen.ErrorResp{Error: To("instance already started")}, nil
	}

	defer func() {
		out = &gen.ErrorResp{}
		if err != nil {
			out.Error = To(err.Error())
			setBoxInstance(nil, nil)
			autoRedirectMark.Store(0)
		}
	}()

	if debugLog {
		log.Println("Start:", *in.CoreConfig)
		if in.XrayConfig != nil {
			log.Println("Start Xray:", *in.XrayConfig)
		}
	}

	if err = startExtraSessions(ctx, in); err != nil {
		return
	}
	defer func() {
		if err != nil {
			stopExtraSessions(false)
			closeXray()
		}
	}()

	autoRedirectMark.Store(autoRedirectMarkFor([]byte(in.GetCoreConfig())))

	// Filled in below, once boxmain.Create has built the box these sidecars resolve through.
	var boxCtx boxContextHolder
	prepareXray := xrayPreparer(in.GetXrayOutboundDnsStrategy(), boxCtx.get)

	if *in.NeedXray {
		if in.GetXrayLazyStart() {
			gate, e := xray.StartGate(*in.XrayConfig,
				time.Duration(in.GetXrayIdleSeconds())*time.Second, prepareXray)
			if e != nil {
				err = e
				return
			}
			setXray(nil, gate)
		} else {
			instance, e := xray.CreateXrayInstance(*in.XrayConfig)
			if e != nil {
				err = e
				return
			}
			if e = prepareXray(instance); e != nil {
				instance.Close()
				err = e
				return
			}
			if e = instance.Start(); e != nil {
				instance.Close()
				err = e
				return
			}
			setXray(instance, nil)
		}
	}

	if fullConfigs := in.GetXrayFullConfigs(); len(fullConfigs) > 0 {
		gates, e := startXrayFullGates(fullConfigs,
			time.Duration(in.GetXrayFullIdleSeconds())*time.Second, prepareXray)
		if e != nil {
			closeXray()
			err = e
			return
		}
		setXrayFullGates(gates)
	}

	box, cancel, err := boxmain.Create([]byte(*in.CoreConfig), boxCtx.publish)
	if err != nil {
		stopExtraSessions(false)
		closeXray()
		return
	}
	setBoxInstance(box, cancel)

	if runtime.GOOS == "darwin" && in.GetTunIpv4Cidr() != "" {
		stopAllCores := func() {
			box.CloseWithTimeout(cancel, time.Second*2, log.Println, true)
			setBoxInstance(nil, nil)
			stopExtraSessions(false)
			closeXray()
		}

		tunCIDR := in.GetTunIpv4Cidr()
		tunPrefix, parseErr := netip.ParsePrefix(tunCIDR)
		if parseErr != nil || !tunPrefix.Addr().Is4() {
			err = fmt.Errorf("invalid tun_ipv4_cidr %q", tunCIDR)
			stopAllCores()
			return
		}

		tunDNS := tunPrefix.Addr()
		if !tunDNS.IsValid() || !tunDNS.Is4() {
			err = fmt.Errorf("got invalid DNS IP from tun_ipv4_cidr: %s", tunDNS)
			stopAllCores()
			return
		}

		if err := sysdns.SetSystemDNS(tunDNS.String(), box.Network().InterfaceMonitor()); err != nil {
			log.Println("Failed to set system DNS:", err)
		}

		needUnsetDNS = true
	}

	if in.GetTunIpv4Cidr() != "" {
		stopEgressForwardingWatch = watchEgressForwarding(box.Network())
	}

	return
}

func (s *server) Stop(ctx context.Context, in *gen.EmptyReq) (out *gen.ErrorResp, _ error) {
	lifecycleMu.Lock()
	defer lifecycleMu.Unlock()

	var err error

	defer func() {
		out = &gen.ErrorResp{}
		if err != nil {
			out.Error = To(err.Error())
		}
	}()

	box, cancel := currentInstance()
	if box == nil {
		stopExtraSessions(false)
		return
	}

	if stopEgressForwardingWatch != nil {
		stopEgressForwardingWatch()
		stopEgressForwardingWatch = nil
	}

	if needUnsetDNS {
		needUnsetDNS = false
		err := sysdns.SetSystemDNS("Empty", box.Network().InterfaceMonitor())
		if err != nil {
			log.Println("Failed to unset system DNS:", err)
		}
	}
	// Unpublished first, so a poll mid-teardown sees no instance rather than a dying one.
	setBoxInstance(nil, nil)
	// Disconnect tunnel transports before removing their TUN routes.
	stopExtraSessions(true)
	box.CloseWithTimeout(cancel, time.Second*2, log.Println, true)

	stopExtraSessions(false)

	closeXray()
	// The Tun and its nftables rules went down with the box, so later test instances must not carry the exemption mark.
	autoRedirectMark.Store(0)

	return
}

func (s *server) CheckConfig(ctx context.Context, in *gen.LoadConfigReq) (out *gen.ErrorResp, _ error) {
	out = &gen.ErrorResp{}
	// boxmain.Check can panic on malformed configs; unrecovered it reaches main()'s os.Exit(0) and kills the core.
	defer func() {
		if r := recover(); r != nil {
			buf := make([]byte, 4096)
			n := runtime.Stack(buf, false)
			log.Printf("CheckConfig panic: %v\n%s", r, buf[:n])
			out.Error = To(fmt.Sprintf("CheckConfig panic: %v", r))
		}
	}()
	if in.GetNeedXray() {
		if err := xray.CheckXrayConfig(in.GetXrayConfig()); err != nil {
			out.Error = To(err.Error())
		}
		return
	}
	err := boxmain.Check([]byte(*in.CoreConfig))
	if err != nil {
		out.Error = To(err.Error())
	}
	return
}
