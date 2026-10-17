//go:build !windows

package main

import "os/exec"

// systemResolvers lists the nameservers the system resolver uses. Android gets
// the network's servers from the app instead (system_resolvers).
func systemResolvers() []string { return resolvConfServers("/etc/resolv.conf") }

func hideChildWindow(*exec.Cmd) {}
