package rpc

import (
	"context"
	"errors"
	"time"

	"ThroneCore/gen"
	"ThroneCore/internal/probe"
)

func (s *server) Test(ctx context.Context, in *gen.TestReq) (*gen.TestResp, error) {
	env, err := prepareTestEnv(in.GetTestCurrent(), in.GetNeedXray(), in.GetXrayConfig(),
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
	defer env.close()

	// Held, not re-read: StopTest rearms a fresh context, uncancelled.
	testCtx := env.ctx
	if in.GetTestCurrent() {
		testCtx = probe.LiveInstance(testCtx)
	}

	// A muxed config needs a warm connection; the live instance already is one.
	twice := !in.GetTestCurrent() || in.GetQwdttConfig() != ""
	var results []*probe.URLTestResult
	if in.GetQwdttConfig() != "" {
		results = probe.BatchURLTestTo(testCtx, env.box, env.tags, in.GetUrl(),
			int(in.GetMaxConcurrency()), twice, time.Duration(in.GetTestTimeoutMs())*time.Millisecond, func(*probe.URLTestResult) {})
	} else {
		results = probe.BatchURLTest(testCtx, env.box, env.tags, in.GetUrl(),
			int(in.GetMaxConcurrency()), twice, time.Duration(in.GetTestTimeoutMs())*time.Millisecond)
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
			OutboundTag: To(env.tags[idx]),
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
	defer env.close()

	timeout := time.Duration(in.GetTestTimeoutMs()) * time.Millisecond
	var results []*probe.IPTestResult
	if in.GetQwdttConfig() != "" {
		results = probe.BatchIPTestTo(env.ctx, env.box, env.tags,
			int(in.GetMaxConcurrency()), !current, timeout, func(*probe.IPTestResult) {})
	} else {
		results = probe.BatchIPTest(env.ctx, env.box, env.tags,
			int(in.GetMaxConcurrency()), !current, timeout)
	}

	res := make([]*gen.IPTestRes, 0, len(results))
	for idx, data := range results {
		errStr := ""
		if data.Error != nil {
			errStr = data.Error.Error()
		}
		res = append(res, &gen.IPTestRes{
			OutboundTag: To(env.tags[idx]),
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
