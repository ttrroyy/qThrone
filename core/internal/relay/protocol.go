package relay

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"github.com/zalando/go-keyring"
	"io"
	"strings"
	"sync"
)

type Request struct {
	Operation string            `json:"operation"`
	ID        string            `json:"id"`
	Value     string            `json:"value"`
	Server    map[string]string `json:"server"`
	Count     int               `json:"count"`
	Hashes    []string          `json:"hashes"`
}
type Event struct {
	Type  string `json:"type"`
	Text  string `json:"text,omitempty"`
	Value any    `json:"value,omitempty"`
}
type protocol struct {
	vkBrowserUsed bool
	input         *bufio.Scanner
	output        *json.Encoder
	mu            sync.Mutex
	secrets       []string
	ctx           context.Context
	commands      chan string
}

func (p *protocol) emit(kind, text string, value any) {
	if kind != "trust" && kind != "host-key" && kind != "admin-pin" {
		for _, secret := range p.secrets {
			if secret != "" {
				text = strings.ReplaceAll(text, secret, "[скрыто]")
			}
		}
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	_ = p.output.Encode(Event{kind, text, value})
}
func Run(input io.Reader, output io.Writer) (err error) {
	p := &protocol{input: bufio.NewScanner(input), output: json.NewEncoder(output)}
	p.input.Buffer(make([]byte, 4096), 2*1024*1024)
	defer func() {
		if err != nil {
			p.emit("error", err.Error(), nil)
		}
	}()
	if !p.input.Scan() {
		return fmt.Errorf("Нет запроса")
	}
	var r Request
	if err = json.Unmarshal(p.input.Bytes(), &r); err != nil {
		return fmt.Errorf("Некорректный запрос")
	}
	var cancel context.CancelFunc
	p.ctx, cancel = context.WithCancel(context.Background())
	defer cancel()
	p.commands = make(chan string, 1)
	go func() {
		defer close(p.commands)
		for p.input.Scan() {
			line := p.input.Text()
			if line == "cancel" {
				cancel()
				return
			}
			select {
			case p.commands <- line:
			case <-p.ctx.Done():
				return
			}
		}
		if r.Operation == "wdttplus-bootstrap" {
			cancel()
		}
	}()
	switch r.Operation {
	case "vk-info":
		var info map[string]any
		info, err = accountInfo()
		if err == nil {
			p.emit("result", "", info)
		}
	case "secret-get":
		var value string
		value, err = secureGet(r.ID)
		if err == keyring.ErrNotFound {
			value = ""
			err = nil
		}
		if err == nil {
			p.emit("result", "", value)
		}
	case "secret-set":
		err = secureSet(r.ID, r.Value)
	case "secret-delete":
		if r.ID == "account" {
			unlock, e := vaultLock(p.ctx, "account")
			if e != nil {
				return e
			}
			defer unlock()
		}
		err = secureDelete(r.ID)
		if err == keyring.ErrNotFound {
			err = nil
		}
	case "deploy", "uninstall":
		err = p.deploy(r)
	case "qwdtt-admin", "wdttplus-admin", "wdttplus-backup-export", "wdttplus-exit":
		// Server management of a deployed server; SSH only with the host key remembered at deploy.
		for _, k := range []string{"sshPassword", "key", "keyPassword", "sudoPassword", "password", "adminToken"} {
			p.secrets = append(p.secrets, r.Server[k])
		}
		var value any
		switch r.Operation {
		case "qwdtt-admin":
			value, err = p.qwdttAdmin(r.Server, r.Value)
		case "wdttplus-admin":
			value, err = p.wdttPlusAdmin(r.Server, r.Value)
		case "wdttplus-exit":
			value, err = p.wdttPlusExit(r.Server, r.Value)
		default:
			value, err = p.wdttPlusBackupExport(r.Server, r.Value)
		}
		if err == nil {
			p.emit("result", "", value)
		}
	case "vk-login":
		err = p.login(r)
	case "vk-hashes":
		err = p.hashes(r)
	case "vk-hashes-delete":
		err = p.deleteHashes(r)
	case "vk-calls-token":
		err = p.callsToken()
	case "wdttplus-bootstrap":
		err = p.wdttPlusBootstrap(r.Value)
	default:
		err = fmt.Errorf("Неизвестная операция")
	}
	if err == nil {
		p.emit("done", "", nil)
	}
	return err
}

func quote(value string) string { return "'" + strings.ReplaceAll(value, "'", "'\"'\"'") + "'" }
