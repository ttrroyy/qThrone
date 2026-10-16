package process

import "log"

const maxObservedLine = 64 << 10

type pipeLogger struct {
	prefix   string
	noOut    bool
	observer func([]byte)
	// Each stream has its own logger, so a line is never interleaved with the other stream.
	pending []byte
}

func (p *pipeLogger) Write(b []byte) (int, error) {
	if p.observer != nil {
		p.observeLines(b)
	}
	if !p.noOut {
		log.Println(p.prefix + ":" + string(b))
	}
	return len(b), nil
}

// The observer sees whole lines, each with its newline, however the pipe split them.
func (p *pipeLogger) observeLines(b []byte) {
	for len(b) > 0 {
		i := 0
		for i < len(b) && b[i] != '\n' {
			i++
		}
		if i == len(b) {
			p.pending = append(p.pending, b...)
			if len(p.pending) > maxObservedLine {
				p.observer(p.pending)
				p.pending = nil
			}
			return
		}
		line := append(p.pending, b[:i+1]...)
		p.pending = nil
		p.observer(line)
		b = b[i+1:]
	}
}
