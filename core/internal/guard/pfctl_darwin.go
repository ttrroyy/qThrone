package guard

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"os/exec"
	"regexp"
	"slices"
	"strings"
	"sync"
	"time"
)

const (
	pfctlPath    = "/sbin/pfctl"
	pfctlTimeout = 5 * time.Second
	pfRootAnchor = "com.apple"
	// /etc/pf.conf evaluates the com.apple/* children in name order: "000." runs before Apple's and the bridge's.
	pfAnchorPrefix = pfRootAnchor + "/000.qthrone-guard."
)

var (
	pfctlAccess        sync.Mutex
	pfAnchorIDPattern  = regexp.MustCompile(`^[0-9a-f]{8}$`)
	errPfctlIncomplete = errors.New("did not finish")
)

type pfctlOutput struct {
	stdout string
	stderr string
	pid    int
}

func pfLockPath(id string) string {
	return "/var/run/qthrone-guard." + id + ".lock"
}

// pfctl returns the output even on failure; an error wrapping errPfctlIncomplete means pfctl never reported a result.
func pfctl(stdin []byte, args ...string) (pfctlOutput, error) {
	pfctlAccess.Lock()
	defer pfctlAccess.Unlock()
	ctx, cancel := context.WithTimeout(context.Background(), pfctlTimeout)
	defer cancel()
	cmd := exec.CommandContext(ctx, pfctlPath, args...)
	cmd.Env = []string{"PATH=/usr/bin:/bin:/usr/sbin:/sbin"}
	if stdin != nil {
		cmd.Stdin = bytes.NewReader(stdin)
	}
	var stdout, stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	err := cmd.Run()
	var output pfctlOutput
	if cmd.Process != nil {
		output.pid = cmd.Process.Pid
	}
	output.stdout, output.stderr = stdout.String(), stderr.String()
	if err != nil {
		command := "pfctl " + strings.Join(args, " ")
		var exitErr *exec.ExitError
		if ctx.Err() != nil || !errors.As(err, &exitErr) {
			err = fmt.Errorf("%s: %w (%v)", command, errPfctlIncomplete, err)
		} else if detail := pfctlDiagnostic(output.stderr); detail != "" {
			err = fmt.Errorf("%s: %v: %s", command, err, detail)
		} else {
			err = fmt.Errorf("%s: %v", command, err)
		}
	}
	return output, err
}

// pfctlDiagnostic drops the ALTQ warnings pfctl prints on every run.
func pfctlDiagnostic(stderr string) string {
	var lines []string
	for line := range strings.Lines(stderr) {
		line = strings.TrimSpace(line)
		if line != "" && !strings.Contains(line, "ALTQ") {
			lines = append(lines, line)
		}
	}
	return strings.Join(lines, "; ")
}

func flushAnchor(anchor string) error {
	_, err := pfctl(nil, "-a", anchor, "-F", "all")
	return err
}

// pfctl -d frees every token and the kernel reissues freed values: a token is ours only while listed under our enabler's pid.
func releaseToken(token string, pid string) error {
	references, err := pfctl(nil, "-s", "References")
	if err != nil {
		return err
	}
	for line := range strings.Lines(references.stdout) {
		fields := strings.Fields(line)
		if len(fields) >= 3 && fields[0] == pid && slices.Contains(fields[1:], token) {
			_, err = pfctl(nil, "-X", token)
			return err
		}
	}
	return fmt.Errorf("pf reference %s of pid %s is no longer held", token, pid)
}
