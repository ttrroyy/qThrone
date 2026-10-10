//go:build windows

package main

import (
	"os"
	"os/exec"
	"testing"
	"time"
)

func TestCSQTTChildOwnerHelper(t *testing.T) {
	if os.Getenv("CSQTT_CHILD_OWNER_TEST") == "1" {
		time.Sleep(time.Minute)
		os.Exit(0)
	}
}

func TestCSQTTChildDiesWithOwner(t *testing.T) {
	exe, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command(exe, "-test.run=^TestCSQTTChildOwnerHelper$")
	cmd.Env = append(os.Environ(), "CSQTT_CHILD_OWNER_TEST=1")
	if err = cmd.Start(); err != nil {
		t.Fatal(err)
	}
	defer cmd.Process.Kill()
	closeOwner, err := ownCSQTTChild(cmd)
	if err != nil {
		t.Fatal(err)
	}
	closeOwner()
	done := make(chan error, 1)
	go func() { done <- cmd.Wait() }()
	select {
	case <-done:
	case <-time.After(3 * time.Second):
		t.Fatal("child outlived its owner")
	}
}
