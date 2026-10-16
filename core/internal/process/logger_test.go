package process

import (
	"reflect"
	"testing"
)

func TestObserverReceivesWholeLines(t *testing.T) {
	var lines []string
	logger := &pipeLogger{noOut: true, observer: func(b []byte) { lines = append(lines, string(b)) }}
	for _, chunk := range []string{"[СТАТИС", "ТИКА] Активных: 9\nnext", " line\n", "a\nb\n", "tail"} {
		_, _ = logger.Write([]byte(chunk))
	}
	want := []string{"[СТАТИСТИКА] Активных: 9\n", "next line\n", "a\n", "b\n"}
	if !reflect.DeepEqual(lines, want) {
		t.Fatalf("lines %q, want %q", lines, want)
	}
}
