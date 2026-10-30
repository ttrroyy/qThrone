package rpc

import (
	"context"
	"sync"
	"time"

	"qThroneCore/gen"
	"qThroneCore/internal/boxbox"

	"github.com/sagernet/sing-box/adapter"
	C "github.com/sagernet/sing-box/constant"
	E "github.com/sagernet/sing/common/exceptions"
	"github.com/sagernet/sing/service"
)

// Matches tags::dnsDirect in the config builder (src/configs/generate.cpp).
const directDNSTag = "dns-direct"

const (
	resolveTimeout     = 5 * time.Second
	resolveConcurrency = 16
)

// Pinned to dns-direct: the OS resolver is what a kill switch blocks, and a running Tun answers it from fakeip.
func (s *server) ResolveDomains(ctx context.Context, in *gen.ResolveDomainsRequest) (*gen.ResolveDomainsResponse, error) {
	domains := in.GetDomains()
	if len(domains) == 0 {
		return &gen.ResolveDomainsResponse{}, nil
	}
	box := currentBox()
	transport := directDNSTransport(box)
	if transport == nil {
		env, err := prepareTestEnv(false, false, "", nil, in.GetConfig(), nil, false, "")
		if err != nil {
			return nil, err
		}
		defer env.close()
		box = env.box
		if transport = directDNSTransport(box); transport == nil {
			return nil, E.New("the probe config has no ", directDNSTag, " server")
		}
	}
	router := service.FromContext[adapter.DNSRouter](box.Context())
	if router == nil {
		return nil, E.New("the sing-box DNS router is not registered")
	}

	results := make([]*gen.ResolvedDomain, len(domains))
	semaphore := make(chan struct{}, resolveConcurrency)
	var wg sync.WaitGroup
	for i, domain := range domains {
		wg.Add(1)
		semaphore <- struct{}{}
		go func() {
			defer func() {
				<-semaphore
				wg.Done()
			}()
			results[i] = resolveDomain(box.Context(), router, transport, domain)
		}()
	}
	wg.Wait()
	return &gen.ResolveDomainsResponse{Results: results}, nil
}

func resolveDomain(ctx context.Context, router adapter.DNSRouter, transport adapter.DNSTransport, domain string) *gen.ResolvedDomain {
	result := &gen.ResolvedDomain{Domain: To(domain)}
	// AsIS defers to route.default_domain_resolver's strategy, the one outbounds dial their servers with.
	addresses, err := router.Lookup(ctx, domain, adapter.DNSQueryOptions{
		Transport: transport,
		Strategy:  C.DomainStrategyAsIS,
		Timeout:   resolveTimeout,
	})
	if err != nil {
		result.Error = To(err.Error())
		return result
	}
	for _, address := range addresses {
		result.Addresses = append(result.Addresses, address.Unmap().String())
	}
	return result
}

// nil when the box has none, which a raw DNS object may leave out.
func directDNSTransport(box *boxbox.Box) adapter.DNSTransport {
	if box == nil {
		return nil
	}
	manager := service.FromContext[adapter.DNSTransportManager](box.Context())
	if manager == nil {
		return nil
	}
	transport, loaded := manager.Transport(directDNSTag)
	if !loaded {
		return nil
	}
	return transport
}
