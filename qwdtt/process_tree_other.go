//go:build !windows

package main

// Elsewhere the captcha browser and transports already watch their parent's lifetime.
func ownProcessTree() {}

func removeStaleCaptchaProfiles() {}
