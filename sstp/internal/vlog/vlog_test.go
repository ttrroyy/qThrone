package vlog

import (
	"bytes"
	"log/slog"
	"regexp"
	"strings"
	"testing"
)

// TestASuppressedLineAllocatesNothing is the guard the plan worried about before
// this package existed: "slog's attribute API allocates unless used carefully,"
// and the drop path on every data path logs through here.
//
// A flood of malformed packets must not start allocating because somebody left a
// logger configured. The Enabled check in logf comes before the Sprintf for
// exactly this, and this test is what stops that ordering being tidied away.
func TestASuppressedLineAllocatesNothing(t *testing.T) {
	l := Discard()
	if n := testing.AllocsPerRun(1000, func() {
		l.Printf("dataplane: decap key %#x failed: %v", 0x1234, "bad tag")
	}); n != 0 {
		t.Errorf("a suppressed line allocated %v times per call; the drop path now allocates "+
			"per bad packet", n)
	}
}

// TestANilLoggerIsUsable pins the contract every "no logger configured" branch
// in the tree relies on: a nil *Logger discards instead of panicking, so the
// fallbacks those branches used to need are gone.
func TestANilLoggerIsUsable(t *testing.T) {
	var l *Logger
	l.Printf("no receiver")
	l.Warnf("still no receiver")
	l.Errorf("nor here")
	if l.Enabled(slog.LevelError) {
		t.Error("a nil logger reports a level as enabled")
	}
	if l.Slog() == nil {
		t.Error("Slog on a nil logger returned nil rather than a discarding logger")
	}
}

// TestTheTextHandlerWritesTheCustomaryLine pins the format the interop cells,
// the panel's log ring and every runbook read. See
// TestTheTextLineIsByteForByteWhatLogPackagePrinted in cmd/veepin for the
// comparison against log.Logger itself.
func TestTheTextHandlerWritesTheCustomaryLine(t *testing.T) {
	var buf bytes.Buffer
	New(slog.New(NewTextHandler(&buf, slog.LevelInfo))).Printf("ikev2: client %s up", "10.0.0.2")

	line := strings.TrimRight(buf.String(), "\n")
	want := regexp.MustCompile(`^\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}\.\d{6} ikev2: client 10\.0\.0\.2 up$`)
	if !want.MatchString(line) {
		t.Errorf("line = %q, want a LstdFlags|Lmicroseconds timestamp then the message alone", line)
	}
	if strings.Contains(line, "level=") || strings.Contains(line, "msg=") {
		t.Errorf("the text format became structured: %q", line)
	}
}

// TestPlainDropsTheTimestamp covers the other constructor: log.New(w, "", 0)
// wrote the message and nothing else, and the callers that read their own output
// back depend on that.
func TestPlainDropsTheTimestamp(t *testing.T) {
	var buf bytes.Buffer
	Plain(&buf).Printf("sstp: %d clients", 3)
	if got := buf.String(); got != "sstp: 3 clients\n" {
		t.Errorf("Plain wrote %q, want the bare message", got)
	}
}

// TestTheLevelDecidesWhatIsWritten is the whole point of the package.
func TestTheLevelDecidesWhatIsWritten(t *testing.T) {
	var buf bytes.Buffer
	l := New(slog.New(NewPlainHandler(&buf, slog.LevelWarn)))

	l.Debugf("debug")
	l.Printf("info")
	if buf.Len() != 0 {
		t.Errorf("at warn, the informational stream printed %q", buf.String())
	}
	l.Warnf("warn")
	l.Errorf("error")
	if got := buf.String(); got != "warn\nerror\n" {
		t.Errorf("at warn, got %q, want the warning and the error", got)
	}
}

// TestAttributesAreNotDroppedSilently covers the case this tree does not use but
// the next person might: a handler that swallowed attrs would lose data with no
// sign of it.
func TestAttributesAreNotDroppedSilently(t *testing.T) {
	var buf bytes.Buffer
	slog.New(NewPlainHandler(&buf, slog.LevelInfo)).With("peer", "10.0.0.2").Info("up", "spi", 7)
	got := buf.String()
	for _, want := range []string{"up", "peer=10.0.0.2", "spi=7"} {
		if !strings.Contains(got, want) {
			t.Errorf("output %q is missing %q", got, want)
		}
	}
}

// TestAPrefixSurvivesEveryLevel covers the NetworkManager service, whose lines
// are "ready" and "exiting" and whose only identity in the journal is the
// prefix — the rest of this tree names its package in the message instead.
func TestAPrefixSurvivesEveryLevel(t *testing.T) {
	var buf bytes.Buffer
	l := New(slog.New(NewPlainHandler(&buf, slog.LevelDebug))).Prefixed("nm-veepin: ")
	l.Printf("ready")
	l.Warnf("bad settings: %v", "no gateway")
	if got := buf.String(); got != "nm-veepin: ready\nnm-veepin: bad settings: no gateway\n" {
		t.Errorf("got %q, want both lines prefixed", got)
	}
}
