//go:build windows

package main

import (
	"bufio"
	"fmt"
	"os"
	"os/exec"
	"strconv"
	"testing"
	"time"

	"golang.org/x/sys/windows"
)

func TestProcessTreeHelper(t *testing.T) {
	switch os.Getenv("QWDTT_PROCESS_TREE_TEST") {
	case "child":
		time.Sleep(time.Minute)
		os.Exit(0)
	case "parent":
		ownProcessTree()
		exe, _ := os.Executable()
		child := exec.Command(exe, "-test.run=^TestProcessTreeHelper$")
		child.Env = append(os.Environ(), "QWDTT_PROCESS_TREE_TEST=child")
		if child.Start() != nil {
			os.Exit(1)
		}
		fmt.Println(child.Process.Pid)
		time.Sleep(time.Minute)
		os.Exit(0)
	}
}

func TestKilledHelperTakesItsChildrenWithIt(t *testing.T) {
	exe, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	parent := exec.Command(exe, "-test.run=^TestProcessTreeHelper$")
	parent.Env = append(os.Environ(), "QWDTT_PROCESS_TREE_TEST=parent")
	out, err := parent.StdoutPipe()
	if err != nil {
		t.Fatal(err)
	}
	if err = parent.Start(); err != nil {
		t.Fatal(err)
	}
	line, err := bufio.NewReader(out).ReadString('\n')
	pid, convErr := strconv.Atoi(trimLine(line))
	if err != nil || convErr != nil {
		_ = parent.Process.Kill()
		t.Fatalf("no child pid: %q %v", line, err)
	}
	child, err := windows.OpenProcess(windows.SYNCHRONIZE, false, uint32(pid))
	if err != nil {
		_ = parent.Process.Kill()
		t.Fatal(err)
	}
	defer windows.CloseHandle(child)
	_ = parent.Process.Kill()
	_ = parent.Wait()
	if event, _ := windows.WaitForSingleObject(child, 5000); event != windows.WAIT_OBJECT_0 {
		t.Fatal("child outlived its killed helper")
	}
}

func trimLine(s string) string {
	for len(s) > 0 && (s[len(s)-1] == '\n' || s[len(s)-1] == '\r') {
		s = s[:len(s)-1]
	}
	return s
}
