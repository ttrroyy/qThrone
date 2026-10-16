package main

import (
	"context"
	"errors"
	"testing"
)

func TestConfigGroupSkipsUnavailableHashes(t *testing.T) {
	var visited []string
	fetch := func(_ context.Context, hash string, stream int) (string, string, []string, error) {
		visited = append(visited, hash)
		if stream != 100 {
			t.Fatal("changed cache stream identity")
		}
		if hash != "valid" {
			return "", "", nil, &CallUnavailableError{Code: 951, Message: "Call not found"}
		}
		return "user", "pass", []string{"turn:relay.invalid"}, nil
	}
	u, p, urls, h, err := fetchGroupCredentials(context.Background(), []string{"dead", "also-dead", "valid"}, 0, 100, true, fetch)
	if err != nil || u != "user" || p != "pass" || len(urls) != 1 || h != "valid" || len(visited) != 3 {
		t.Fatalf("fallback: %s %v %v", h, visited, err)
	}
	visited = nil
	_, _, _, _, err = fetchGroupCredentials(context.Background(), []string{"dead", "valid"}, 0, 100, false, fetch)
	if err == nil || len(visited) != 1 {
		t.Fatal("ordinary worker distribution changed")
	}
}

func TestConfigGroupStopsFallbackOnCancellation(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	calls := 0
	_, _, _, _, err := fetchGroupCredentials(ctx, []string{"first", "second"}, 0, 100, true, func(context.Context, string, int) (string, string, []string, error) {
		calls++
		cancel()
		return "", "", nil, context.Canceled
	})
	if !errors.Is(err, context.Canceled) || calls != 1 {
		t.Fatalf("cancellation: calls=%d err=%v", calls, err)
	}
}

func TestConfigGroupDoesNotMultiplyCaptchaOrNetworkFailures(t *testing.T) {
	for _, failure := range []error{errors.New("CAPTCHA_WAIT_REQUIRED"), errors.New("network timeout")} {
		calls := 0
		_, _, _, _, err := fetchGroupCredentials(context.Background(), []string{"first", "second", "third"}, 0, 100, true, func(context.Context, string, int) (string, string, []string, error) {
			calls++
			return "", "", nil, failure
		})
		if err != failure || calls != 1 {
			t.Fatal("non-call failure triggered extra VK hash requests")
		}
	}
}
