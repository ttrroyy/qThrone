package relay

import (
	"encoding/json"
	"errors"
	"io"
	"net"
	"strconv"
	"strings"
)

// Only the unambiguous saved VPS with an approved host key may relay enrollment.
func wdttPlusBootstrapServer(peer string, servers []map[string]string) map[string]string {
	host, port, err := net.SplitHostPort(peer)
	if err != nil {
		return nil
	}
	var match map[string]string
	for _, s := range servers {
		sshPort, _ := strconv.Atoi(s["sshPort"])
		authenticated := s["sshPassword"] != ""
		if s["auth"] == "key" {
			authenticated = s["key"] != ""
		}
		if s["kind"] != "wdttplus" || !strings.EqualFold(strings.Trim(s["host"], "[]"), host) || s["peerPort"] != port ||
			!strings.HasPrefix(s["hostKey"], "SHA256:") || s["user"] == "" || sshPort < 1 || sshPort > 65535 || !authenticated {
			continue
		}
		if match != nil {
			return nil
		}
		match = s
	}
	return match
}

func (p *protocol) wdttPlusBootstrap(peer string) error {
	raw, err := secureGet("servers")
	if err != nil {
		return errors.New("Нет сохранённого доступа к VPS для регистрации MASQUE")
	}
	var entries []map[string]json.RawMessage
	if json.Unmarshal([]byte(raw), &entries) != nil {
		return errors.New("Некорректный список VPS")
	}
	var servers []map[string]string
	for _, entry := range entries {
		s := map[string]string{}
		for key, value := range entry {
			var text string
			if json.Unmarshal(value, &text) == nil {
				s[key] = text
			}
		}
		servers = append(servers, s)
	}
	s := wdttPlusBootstrapServer(peer, servers)
	if s == nil {
		return errors.New("Нет однозначного сохранённого VPS для регистрации MASQUE")
	}
	client, err := p.dial(s, false)
	if err != nil {
		return errors.New("SSH-регистрация MASQUE недоступна")
	}
	defer client.Close()
	listener, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		return err
	}
	defer listener.Close()
	go func() { <-p.ctx.Done(); listener.Close(); client.Close() }()
	p.emit("result", "", listener.Addr().String())
	for {
		local, err := listener.Accept()
		if err != nil {
			return nil
		}
		go func() {
			defer local.Close()
			// TLS remains end to end; no arbitrary destinations or SSH secrets leave this process.
			remote, err := client.Dial("tcp", "api.cloudflareclient.com:443")
			if err != nil {
				return
			}
			defer remote.Close()
			done := make(chan struct{})
			go func() { io.Copy(remote, local); remote.Close(); close(done) }()
			io.Copy(local, remote)
			local.Close()
			<-done
		}()
	}
}
