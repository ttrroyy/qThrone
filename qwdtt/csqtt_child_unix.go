//go:build !windows

package main

import "os/exec"

// The original Unix transport already monitors its parent's lifetime.
func ownCSQTTChild(*exec.Cmd) (func(), error) { return func() {}, nil }
