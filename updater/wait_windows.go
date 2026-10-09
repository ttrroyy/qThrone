package main

import (
	"fmt"
	"syscall"
)

func waitParent(pid int) error {
	kernel := syscall.NewLazyDLL("kernel32.dll")
	handle, _, err := kernel.NewProc("OpenProcess").Call(0x00100000, 0, uintptr(pid))
	if handle == 0 {
		if err == syscall.Errno(87) {
			return nil
		}
		return fmt.Errorf("open parent: %w", err)
	}
	defer kernel.NewProc("CloseHandle").Call(handle)
	result, _, err := kernel.NewProc("WaitForSingleObject").Call(handle, 60000)
	if result != 0 {
		return fmt.Errorf("waiting for qThrone to exit: result %d (%v)", result, err)
	}
	return nil
}
