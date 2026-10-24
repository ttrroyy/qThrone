package rpc

import (
	"context"
	"encoding/json"
	"errors"
	"time"

	"qThroneCore/gen"
	"qThroneCore/internal/probe"
)

func (s *server) Test(ctx context.Context, in *gen.TestReq) (*gen.TestResp, error) {
	// qWDTT is measured like the original app does it: one TURN/DTLS handshake with the
	// server, no tunnel. A sweep through full tunnels hit VK's Flood control and ended
	// with a device-wide disconnect that also dropped the connected VPN.
	if nativeQWDTTPing(in.GetQwdttConfig()) {
		// Stopping tests must end this handshake too, or a repeated ping waits for the old one.
		pingCtx, cancelPing := context.WithCancel(probe.TestContext())
		defer cancelPing()
		defer context.AfterFunc(ctx, cancelPing)()
		delay, err := runQWDTTPing(pingCtx, in.GetQwdttConfig(), time.Duration(in.GetTestTimeoutMs())*time.Millisecond)
		message := ""
		if err != nil {
			message = "qWDTT ping failed: " + err.Error()
		}
		return &gen.TestResp{Results: []*gen.URLTestResp{{OutboundTag: To("proxy"), LatencyMs: To(delay), Error: To(message)}}}, nil
	}
	// Each RPC owns a deadline/cancellation scope. Expiring one native probe
	// must not cancel sibling requests or leave a helper running after its caller.
	parent, cancel := context.WithCancel(probe.TestContext())
	defer cancel()
	stopRPC := context.AfterFunc(ctx, cancel)
	defer stopRPC()
	env, err := prepareTestEnvContext(parent, in.GetTestCurrent(), in.GetNeedXray(), in.GetXrayConfig(),
		in.XrayFullConfigs, in.GetConfig(), in.OutboundTags, in.GetUseDefaultOutbound(),
		in.GetXrayOutboundDnsStrategy(), in.GetQwdttConfig())
	if err != nil {
		if in.GetQwdttConfig() != "" {
			return &gen.TestResp{Results: []*gen.URLTestResp{{OutboundTag: To("proxy"), LatencyMs: To(int32(0)), Error: To("qWDTT test not started: " + err.Error())}}}, nil
		}
		if errors.Is(err, errInstanceNotRunning) {
			return &gen.TestResp{Results: []*gen.URLTestResp{{
				OutboundTag: To("proxy"),
				LatencyMs:   To(int32(0)),
				Error:       To(err.Error()),
			}}}, nil
		}
		return nil, err
	}
	defer closeTestEnv(env, in.GetQwdttConfig())

	// Held, not re-read: StopTest rearms a fresh context, uncancelled. A native helper's queue and
	// startup have budgets of their own, so this one covers only the requests.
	testCtx, finish := context.WithTimeout(env.ctx, urlProbeBudget(in))
	defer finish()
	if env.live {
		testCtx = probe.LiveInstance(testCtx)
	}

	// A live tunnel is warm, but each test still owns a new HTTP/TLS connection.
	// Measure its second request just like the standalone tunnel test does.
	twice := !env.live || in.GetQwdttConfig() != ""
	var results []*probe.URLTestResult
	if in.GetQwdttConfig() != "" {
		var transport struct {
			Backend string `json:"backend"`
		}
		_ = json.Unmarshal([]byte(in.GetQwdttConfig()), &transport)
		var coldAllowance time.Duration
		if _, native := transportKinds[transport.Backend]; twice && !env.live && native {
			// SOCKS hides the userspace tunnel from the ordinary VPN startup allowance.
			// Warm it once; the measured request still uses the configured timeout.
			coldAllowance = probe.TunnelStartupTimeout
		}
		results = probe.BatchURLTestTo(testCtx, env.box, env.tags, in.GetUrl(),
			int(in.GetMaxConcurrency()), twice, time.Duration(in.GetTestTimeoutMs())*time.Millisecond, func(*probe.URLTestResult) {}, coldAllowance)
	} else {
		// A shared box streams each finished tag (unique per profile) to QueryURLTest; a single-profile box
		// reports under its "proxy" alias, which concurrent sweeps would share, so it only answers at the end.
		publish := func(*probe.URLTestResult) {}
		if env.reportTag == "" {
			publish = probe.URLReporter.AddResult
		}
		results = probe.BatchURLTestTo(testCtx, env.box, env.tags, in.GetUrl(),
			int(in.GetMaxConcurrency()), twice, time.Duration(in.GetTestTimeoutMs())*time.Millisecond, publish)
		if env.reportTag == "" {
			probe.URLReporter.Reclaim(results)
		}
	}

	res := make([]*gen.URLTestResp, 0, len(results))
	failed := make(map[string]bool, len(results))
	for idx, data := range results {
		errStr := ""
		if data.Error != nil {
			errStr = data.Error.Error()
		}
		failed[env.tags[idx]] = errStr != ""
		res = append(res, &gen.URLTestResp{
			OutboundTag: To(env.resultTag(env.tags[idx])),
			LatencyMs:   To(int32(data.Duration.Milliseconds())),
			Error:       To(errStr),
		})
	}

	out := &gen.TestResp{Results: res}
	// `defer env.close()` tears the box down on return, so this cannot wait.
	var pending []string
	for _, tag := range in.VpnEndpointTags {
		if failed[tag] {
			pending = append(pending, tag)
		}
	}
	if len(pending) > 0 {
		// A snapshot: the probe already sat out the handshake, so a tunnel that settles only now never carried it.
		out.VpnStatus = collectVPNStatus(testCtx, env.box, pending, 0)
	}
	return out, nil
}

// A repeated ping waits for this one to return, and Xray alone can take seconds to close; the box
// is torn down in the background instead. A native helper still closes before the next one starts.
func closeTestEnv(env *testEnv, qwdttConfig string) {
	if qwdttConfig != "" {
		env.close()
		return
	}
	go env.close()
}

func urlProbeBudget(in *gen.TestReq) time.Duration {
	timeout := time.Duration(in.GetTestTimeoutMs()) * time.Millisecond
	if timeout <= 0 {
		timeout = probe.URLTestTimeout
	}
	concurrency := int(in.GetMaxConcurrency())
	if concurrency <= 0 || concurrency >= 500 {
		concurrency = 100
	}
	count := max(1, len(in.OutboundTags))
	rounds := (count + concurrency - 1) / concurrency
	budget := 30*time.Second + time.Duration(rounds)*(2*timeout+probe.TunnelHandshakeTimeout+probe.TunnelStartupTimeout)
	if in.GetQwdttConfig() != "" {
		budget += 5 * time.Minute
	}
	return budget
}

func (s *server) StopTest(ctx context.Context, in *gen.EmptyReq) (*gen.EmptyResp, error) {
	probe.CancelTests()

	return &gen.EmptyResp{}, nil
}

func (s *server) QueryURLTest(ctx context.Context, in *gen.EmptyReq) (out *gen.QueryURLTestResponse, _ error) {
	results := probe.URLReporter.Results()
	out = &gen.QueryURLTestResponse{}
	for _, r := range results {
		errStr := ""
		if r.Error != nil {
			errStr = r.Error.Error()
		}
		out.Results = append(out.Results, &gen.URLTestResp{
			OutboundTag: To(r.Tag),
			LatencyMs:   To(int32(r.Duration.Milliseconds())),
			Error:       To(errStr),
		})
	}
	return
}

func (s *server) IPTest(ctx context.Context, in *gen.IPTestRequest) (*gen.IPTestResp, error) {
	// qWDTT can reuse its active IP/netstack; ordinary IP tests still build their own box.
	current := in.GetTestCurrent()
	env, err := prepareTestEnv(current, in.GetNeedXray(), in.GetXrayConfig(),
		in.XrayFullConfigs, in.GetConfig(), in.OutboundTags, in.GetUseDefaultOutbound(),
		in.GetXrayOutboundDnsStrategy(), in.GetQwdttConfig())
	if err != nil {
		if in.GetQwdttConfig() != "" {
			return &gen.IPTestResp{Results: []*gen.IPTestRes{{OutboundTag: To("proxy"), Error: To(err.Error())}}}, nil
		}
		return nil, err
	}
	defer closeTestEnv(env, in.GetQwdttConfig())

	timeout := time.Duration(in.GetTestTimeoutMs()) * time.Millisecond
	var results []*probe.IPTestResult
	if in.GetQwdttConfig() != "" {
		results = probe.BatchIPTestTo(env.ctx, env.box, env.tags,
			int(in.GetMaxConcurrency()), !env.live, timeout, func(*probe.IPTestResult) {})
	} else {
		results = probe.BatchIPTestTo(env.ctx, env.box, env.tags,
			int(in.GetMaxConcurrency()), !env.live, timeout, func(*probe.IPTestResult) {})
	}

	res := make([]*gen.IPTestRes, 0, len(results))
	for idx, data := range results {
		errStr := ""
		if data.Error != nil {
			errStr = data.Error.Error()
		}
		res = append(res, &gen.IPTestRes{
			OutboundTag: To(env.resultTag(env.tags[idx])),
			Ip:          To(data.Result.IP),
			CountryCode: To(data.Result.CountryCode),
			Error:       To(errStr),
		})
	}
	return &gen.IPTestResp{Results: res}, nil
}

func (s *server) QueryIPTest(ctx context.Context, in *gen.EmptyReq) (out *gen.QueryIPTestResponse, _ error) {
	results := probe.IPReporter.Results()
	out = &gen.QueryIPTestResponse{}
	for _, r := range results {
		errStr := ""
		if r.Error != nil {
			errStr = r.Error.Error()
		}
		out.Results = append(out.Results, &gen.IPTestRes{
			OutboundTag: To(r.Tag),
			Ip:          To(r.Result.IP),
			CountryCode: To(r.Result.CountryCode),
			Error:       To(errStr),
		})
	}
	return
}
