package main

import (
	"bufio"
	"context"
	"encoding/json"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"time"
)

func startWdttPlusBootstrap(ctx context.Context, peer string) (string, func()) {
	exe, err := os.Executable()
	if err != nil {
		return "", func() {}
	}
	name := "qThroneCore"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	command := exec.Command(filepath.Join(filepath.Dir(exe), name), "--relay")
	input, err := command.StdinPipe()
	if err != nil {
		return "", func() {}
	}
	output, err := command.StdoutPipe()
	if err != nil {
		input.Close()
		return "", func() {}
	}
	if err = command.Start(); err != nil {
		input.Close()
		return "", func() {}
	}
	closeOwner, err := ownCSQTTChild(command)
	if err != nil {
		command.Process.Kill()
		command.Wait()
		input.Close()
		return "", func() {}
	}
	done := make(chan struct{})
	go func() { command.Wait(); close(done) }()
	stop := func() {
		defer closeOwner()
		io.WriteString(input, "cancel\n")
		input.Close()
		select {
		case <-done:
		case <-time.After(2 * time.Second):
			command.Process.Kill()
			<-done
		}
	}
	json.NewEncoder(input).Encode(map[string]string{"operation": "wdttplus-bootstrap", "value": peer})
	result := make(chan string, 1)
	go func() {
		scanner := bufio.NewScanner(output)
		for scanner.Scan() {
			var event struct {
				Type  string
				Value string
			}
			if json.Unmarshal(scanner.Bytes(), &event) != nil {
				continue
			}
			if event.Type == "result" && loopbackAddress(event.Value) {
				result <- event.Value
				return
			}
		}
		result <- ""
	}()
	select {
	case address := <-result:
		return address, stop
	case <-ctx.Done():
		stop()
		return "", func() {}
	case <-time.After(35 * time.Second):
		stop()
		return "", func() {}
	}
}
