package process

import (
	"fmt"
	"os"
	"os/exec"
	"strings"
	"time"

	"github.com/sagernet/sing/common/atomic"
)

const extraCorePrefix = "Extra Core"

type Process struct {
	path            string
	args            []string
	noOut           bool
	backgroundProbe bool
	outputObserver  func([]byte)
	cleanupPath     string
	run             running
	stopped         atomic.Bool
	stopCommand     string
	stdin           *os.File
	done            chan struct{}
}

type running interface {
	Wait() error
	Kill() error
}

func NewProcess(path string, args []string, noOut bool) *Process {
	return &Process{path: path, args: args, noOut: noOut}
}

// The path is removed recursively when the process stops, exits, or fails to start; "" means nothing to clean up.
func (p *Process) SetCleanupPath(path string) {
	p.cleanupPath = path
}

// EnableStdinShutdown is opt-in; ordinary extra cores retain their kill behavior.
func (p *Process) EnableStdinShutdown(command string) { p.stopCommand = command }

// Probe failures belong to the test RPC, not the active profile crash handler.
func (p *Process) SetBackgroundProbe() { p.backgroundProbe = true }

func (p *Process) SetOutputObserver(observer func([]byte)) { p.outputObserver = observer }

func (p *Process) Start() error {
	var input *os.File
	if p.stopCommand != "" {
		var err error
		input, p.stdin, err = os.Pipe()
		if err != nil {
			p.cleanup()
			return err
		}
		defer input.Close()
	}
	run, err := startChild(p.path, p.args, p.noOut, input, p.outputObserver)
	if err != nil {
		if p.stdin != nil {
			_ = p.stdin.Close()
		}
		p.cleanup()
		return err
	}
	p.run = run
	p.stopped.Store(false)
	p.done = make(chan struct{})

	go func() {
		defer close(p.done)
		if p.stdin != nil {
			defer p.stdin.Close()
		}
		fmt.Println(p.path, ":", "process started, waiting for it to end")
		_ = p.run.Wait()
		if !p.stopped.Load() {
			if p.backgroundProbe {
				fmt.Println("Background probe process exited")
			} else {
				fmt.Println("Extra process exited unexpectedly")
			}
		}
		p.cleanup()
	}()
	return nil
}

func (p *Process) ExecutablePath() string { return p.path }

func (p *Process) Done() <-chan struct{} { return p.done }

func (p *Process) Stop() {
	p.stopped.Store(true)
	if p.run != nil {
		if p.stdin != nil {
			_, _ = p.stdin.WriteString(p.stopCommand + "\n")
			_ = p.stdin.Close()
			select {
			case <-p.done:
				p.cleanup()
				return
			case <-time.After(3 * time.Second):
			}
		}
		_ = p.run.Kill()
		if p.stopCommand != "" {
			select {
			case <-p.done:
			case <-time.After(time.Second):
			}
		}
	}
	p.cleanup()
}

func newCmd(path string, args []string, noOut bool, input *os.File, observers ...func([]byte)) *exec.Cmd {
	cmd := exec.Command(path, args...)
	var observer func([]byte)
	if len(observers) > 0 {
		observer = observers[0]
	}
	cmd.Stdout = &pipeLogger{prefix: extraCorePrefix, noOut: noOut, observer: observer}
	cmd.Stderr = &pipeLogger{prefix: extraCorePrefix, noOut: noOut, observer: observer}
	cmd.Env = childEnv()
	if input != nil {
		cmd.Stdin = input
	}
	return cmd
}

func startCmd(cmd *exec.Cmd) (running, error) {
	if err := cmd.Start(); err != nil {
		return nil, err
	}
	return &cmdRunner{cmd: cmd}, nil
}

type cmdRunner struct{ cmd *exec.Cmd }

func (c *cmdRunner) Wait() error { return c.cmd.Wait() }
func (c *cmdRunner) Kill() error { return c.cmd.Process.Kill() }

// Safe to call repeatedly and from multiple goroutines; unlink does not traverse a final symlink.
func (p *Process) cleanup() {
	if p.cleanupPath != "" {
		_ = os.RemoveAll(p.cleanupPath)
	}
}

// Returns (configPath, cleanupPath): what the extra process reads, and what Process must remove afterwards.
func CreateExtraConfig(content string) (string, string, error) {
	f, cleanupPath, err := createSecureConfigFile()
	if err != nil {
		return "", "", err
	}
	configPath := f.Name()

	fail := func(e error) (string, string, error) {
		_ = f.Close()
		_ = os.RemoveAll(cleanupPath)
		return "", "", e
	}

	if _, err = f.WriteString(content); err != nil {
		return fail(err)
	}
	// Done on the fd, before Close, so a path swap cannot redirect this privileged chmod (TOCTOU).
	if err = makeConfigReadable(f); err != nil {
		return fail(err)
	}
	if err = f.Close(); err != nil {
		_ = os.RemoveAll(cleanupPath)
		return "", "", err
	}
	return configPath, cleanupPath, nil
}

func childEnv() []string {
	parent := os.Environ()
	out := make([]string, 0, len(parent))
	for _, kv := range parent {
		if name, _, ok := strings.Cut(kv, "="); ok && strings.HasPrefix(strings.ToUpper(name), "THRONE") {
			continue
		}
		out = append(out, kv)
	}
	return out
}
