package process

import "log"

type pipeLogger struct {
	prefix   string
	noOut    bool
	observer func([]byte)
}

func (p *pipeLogger) Write(b []byte) (int, error) {
	if p.observer != nil {
		p.observer(b)
	}
	if !p.noOut {
		log.Println(p.prefix + ":" + string(b))
	}
	return len(b), nil
}
