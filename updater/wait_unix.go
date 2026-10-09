//go:build !windows

package main

import (
	"fmt"
	"os"
	"syscall"
	"time"
)

func waitParent(pid int) error {
	process, err := os.FindProcess(pid)
	if err != nil {
		return err
	}
	defer process.Release()
	deadline := time.Now().Add(time.Minute)
	for time.Now().Before(deadline) {
		err := process.Signal(syscall.Signal(0))
		if err == os.ErrProcessDone || err == syscall.ESRCH {
			return nil
		}
		if err != nil {
			return err
		}
		time.Sleep(100 * time.Millisecond)
	}
	return fmt.Errorf("qThrone did not exit within 60 seconds")
}
