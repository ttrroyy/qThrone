package rpc

import "time"

// transportKind describes how qThrone runs one native transport backend.
type transportKind struct {
	// executable is the bundled helper, without the Windows suffix.
	executable string
	// flag passes the private configuration file.
	flag string
	// vk marks the transports that ask VK for credentials and share its budget.
	vk bool
	// probeStartup bounds a test helper's start, queue wait included.
	probeStartup time.Duration
	// connectStartup bounds the user's connection until its listener answers.
	connectStartup time.Duration
	// quiet helpers' output may name rooms, documents or keys: it stays out of the log.
	quiet bool
}

var transportKinds = map[string]transportKind{
	"sstp":  {executable: "sstp-client", flag: "--qthrone-config", probeStartup: 45 * time.Second, connectStartup: 45 * time.Second, quiet: true},
	"":      {executable: "qwdtt", flag: "-config", vk: true, probeStartup: 5 * time.Minute, connectStartup: 5 * time.Minute},
	"csqtt": {executable: "qwdtt", flag: "-csqtt-config", vk: true, probeStartup: 5 * time.Minute, connectStartup: 5 * time.Minute},
	// WDTT Plus runs its own client behind the qwdtt helper, which answers its captcha.
	"wdttplus": {executable: "qwdtt", flag: "-wdttplus-config", vk: true, probeStartup: 5 * time.Minute, connectStartup: 5 * time.Minute},
	"openflux": {executable: "openflux", flag: "--qthrone-config", probeStartup: 15 * time.Second, connectStartup: 2 * time.Minute, quiet: true},
	"olcrtc":   {executable: "olcrtc", flag: "--qthrone-config", probeStartup: 30 * time.Second, connectStartup: 2 * time.Minute, quiet: true},
	// DNS tunnels measure MTU and handshake query by query; S3 Bypass waits for the bucket.
	"dnstt":      {executable: "dnstun", flag: "--qthrone-config", probeStartup: 2 * time.Minute, connectStartup: 3 * time.Minute, quiet: true},
	"vaydns":     {executable: "dnstun", flag: "--qthrone-config", probeStartup: 2 * time.Minute, connectStartup: 3 * time.Minute, quiet: true},
	"slipstream": {executable: "dnstun", flag: "--qthrone-config", probeStartup: 2 * time.Minute, connectStartup: 3 * time.Minute, quiet: true},
	"masterdns":  {executable: "masterdns", flag: "--qthrone-config", probeStartup: 2 * time.Minute, connectStartup: 3 * time.Minute, quiet: true},
	"cottendns":  {executable: "cottendns", flag: "--qthrone-config", probeStartup: 2 * time.Minute, connectStartup: 3 * time.Minute, quiet: true},
	"soar":       {executable: "soar", flag: "--qthrone-config", probeStartup: 2 * time.Minute, connectStartup: 3 * time.Minute, quiet: true},
	"s3bypass":   {executable: "s3bypass", flag: "--qthrone-config", probeStartup: 2 * time.Minute, connectStartup: 3 * time.Minute, quiet: true},
}

// tunnelHelpers are the bundled executables that may run as routed transports.
var tunnelHelpers = map[string]bool{"qwdtt": true, "openflux": true, "olcrtc": true, "dnstun": true, "masterdns": true, "cottendns": true, "soar": true, "s3bypass": true, "sstp-client": true}
