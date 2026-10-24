//go:build windows

package main

import (
	"os"
	"path/filepath"
	"strings"
	"time"
	"unsafe"

	"golang.org/x/sys/windows"
)

// Windows does not end a process's children with it. A killed helper (a stopped or repeated
// ping) left its captcha browser running, and dozens of hidden Edge windows piled up.
// The helper joins a job that only it holds open, so everything it starts dies with it.
func ownProcessTree() {
	job, err := windows.CreateJobObject(nil, nil)
	if err != nil {
		return
	}
	limits := windows.JOBOBJECT_EXTENDED_LIMIT_INFORMATION{}
	limits.BasicLimitInformation.LimitFlags = windows.JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
	if _, err = windows.SetInformationJobObject(job, windows.JobObjectExtendedLimitInformation,
		uintptr(unsafe.Pointer(&limits)), uint32(unsafe.Sizeof(limits))); err != nil {
		windows.CloseHandle(job)
		return
	}
	if windows.AssignProcessToJobObject(job, windows.CurrentProcess()) != nil {
		windows.CloseHandle(job)
	}
	// The handle stays open for the helper's lifetime; the system closes it on exit.
}

// Captcha profiles of helpers killed before their cleanup ran. A live session lasts at most
// three minutes, so an older profile belongs to nobody.
func removeStaleCaptchaProfiles() {
	entries, err := os.ReadDir(os.TempDir())
	if err != nil {
		return
	}
	for _, entry := range entries {
		if !entry.IsDir() || !strings.HasPrefix(entry.Name(), "qthrone-captcha-") {
			continue
		}
		if info, err := entry.Info(); err == nil && time.Since(info.ModTime()) > 10*time.Minute {
			_ = os.RemoveAll(filepath.Join(os.TempDir(), entry.Name()))
		}
	}
}
