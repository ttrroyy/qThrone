package rpc

import (
	"context"
	"testing"

	"qThroneCore/gen"
)

func TestProbeLaunchedBeforeStopIsRefusedWhenItArrivesLate(t *testing.T) {
	defer stoppedEpoch.Store(0)
	if _, err := globalServer.StopTest(context.Background(), &gen.StopTestReq{Epoch: To(int64(5))}); err != nil {
		t.Fatal(err)
	}
	// An older stop arriving last must not lower the count.
	if _, err := globalServer.StopTest(context.Background(), &gen.StopTestReq{Epoch: To(int64(3))}); err != nil {
		t.Fatal(err)
	}
	resp, err := globalServer.Test(context.Background(), &gen.TestReq{StopEpoch: To(int64(4))})
	if err != nil || len(resp.Results) != 0 {
		t.Fatalf("late probe ran: %v %v", resp, err)
	}
	ip, err := globalServer.IPTest(context.Background(), &gen.IPTestRequest{StopEpoch: To(int64(4))})
	if err != nil || len(ip.Results) != 0 {
		t.Fatalf("late IP probe ran: %v %v", ip, err)
	}
	if launchedBeforeStop(To(int64(5))) || launchedBeforeStop(nil) {
		t.Fatal("a probe launched after the stop, or one without a count, was refused")
	}
}
