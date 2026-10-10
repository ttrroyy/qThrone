// Package csqtt describes the original amurcanov client's public interfaces.
// It does not implement or replace its TURN/RTP wire protocol.
package csqtt

import (
	"errors"
	"net"
	"net/netip"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"unicode"
)

type Link struct {
	Host     string
	Port     int
	Password string
	Hashes   []string
}

func (l Link) Peer() string { return net.JoinHostPort(l.Host, strconv.Itoa(l.Port)) }

// Decode components after splitting hashes: literal + is a separator, %2B
// belongs to a hash. QueryUnescape would incorrectly turn + into a space.
func ParseLink(raw string) (Link, error) {
	invalid := errors.New("invalid CSQTT link")
	u, err := url.Parse(strings.TrimSpace(raw))
	if err != nil || !strings.EqualFold(u.Scheme, "csqtt") || strings.Contains(raw, "#") {
		return Link{}, invalid
	}
	var l Link
	if strings.EqualFold(u.Hostname(), "connect") {
		if u.User != nil || u.Port() != "" || u.Path != "" {
			return l, invalid
		}
		params := map[string]string{}
		for _, part := range strings.FieldsFunc(strings.ReplaceAll(u.RawQuery, "&amp;", "&"), func(r rune) bool { return r == '&' || r == ';' }) {
			key, value, ok := strings.Cut(part, "=")
			if ok {
				params[key] = value
			}
		}
		decode := func(key string) string {
			value, e := url.PathUnescape(params[key])
			if e != nil {
				return ""
			}
			return value
		}
		if params["v"] == "" || params["host"] == "" || params["peer"] == "" || params["password"] == "" {
			// The original Android importer accepts older concatenated parameters.
			pattern := regexp.MustCompile(`(?i)(v|host|peer|password|hashes)=`)
			matches := pattern.FindAllStringSubmatchIndex(u.RawQuery, -1)
			for i, m := range matches {
				end := len(u.RawQuery)
				if i+1 < len(matches) {
					end = matches[i+1][0]
				}
				params[strings.ToLower(u.RawQuery[m[2]:m[3]])] = strings.TrimRight(u.RawQuery[m[1]:end], "&;")
			}
		}
		if decode("v") != "2" {
			return l, invalid
		}
		l.Host, l.Password = decode("host"), decode("password")
		l.Port, err = strconv.Atoi(decode("peer"))
		if err != nil {
			return l, invalid
		}
		if value, present := params["hashes"]; present {
			parts := strings.Split(value, "+")
			if len(parts) > 6 {
				parts = parts[:6]
			}
			seen := map[string]bool{}
			for _, part := range parts {
				hash, e := url.PathUnescape(part)
				if e != nil {
					return l, invalid
				}
				hash = stripHashURL(hash)
				if len(hash) < 16 || strings.IndexFunc(hash, unicode.IsSpace) >= 0 || seen[hash] {
					return l, invalid
				}
				seen[hash] = true
				l.Hashes = append(l.Hashes, hash)
			}
		}
	} else {
		if u.User == nil {
			return l, invalid
		}
		l.Host = u.Hostname()
		l.Password, err = url.PathUnescape(u.User.String())
		if err != nil {
			return l, invalid
		}
		l.Port, err = strconv.Atoi(u.Port())
		if err != nil {
			return l, invalid
		}
	}
	if l.Host == "" || l.Password == "" || l.Port < 1 || l.Port > 65535 || strings.IndexFunc(l.Host+l.Password, unicode.IsSpace) >= 0 {
		return Link{}, invalid
	}
	return l, nil
}

func stripHashURL(hash string) string {
	hash = strings.TrimSpace(hash)
	for _, host := range []string{"vk.com", "vk.ru", "m.vk.com", "m.vk.ru"} {
		for _, scheme := range []string{"https://", "http://", ""} {
			prefix := scheme + host + "/call/join/"
			if strings.HasPrefix(strings.ToLower(hash), prefix) {
				hash = hash[len(prefix):]
				break
			}
		}
	}
	if i := strings.IndexAny(hash, "?#"); i >= 0 {
		hash = hash[:i]
	}
	return strings.TrimRight(hash, "/")
}

func NormalizeWorkers(requested, hashCount int, redistribute bool) int {
	requested = max(9, min(requested, 126)) / 9 * 9
	if !redistribute {
		requested = min(requested, max(1, min(hashCount, 6))*27)
	}
	return requested
}

type TunnelConfig struct {
	Address    netip.Addr
	DNS        []netip.Addr
	ClientPort int
	Revision   string
}

// The third value is the client's UDP port, not an MTU. Android uses MTU 1300.
func ParseTunnelConfig(raw string) (TunnelConfig, error) {
	var c TunnelConfig
	invalid := errors.New("invalid CSQTT IP tunnel configuration")
	parts := strings.Split(raw, ":")
	if len(parts) != 5 || parts[0] != "TUNCONF" {
		return c, invalid
	}
	var err error
	c.Address, err = netip.ParseAddr(parts[1])
	if err != nil || !c.Address.Is4() || c.Address.IsUnspecified() {
		return c, invalid
	}
	c.ClientPort, err = strconv.Atoi(parts[3])
	if err != nil || c.ClientPort < 0 || c.ClientPort > 65535 {
		return c, invalid
	}
	for _, item := range strings.Split(parts[2], ",") {
		a, e := netip.ParseAddr(item)
		if e != nil || a.IsUnspecified() {
			return c, invalid
		}
		c.DNS = append(c.DNS, a)
	}
	c.Revision = parts[4]
	if c.Revision == "" {
		return c, invalid
	}
	return c, nil
}
