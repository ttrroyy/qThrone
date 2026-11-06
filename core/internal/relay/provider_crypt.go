package relay

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	"crypto/rsa"
	"crypto/x509"
	_ "embed"
	"encoding/base64"
	"encoding/json"
	"errors"
	"net/url"
	"regexp"
	"strconv"
	"strings"
	"unicode/utf8"

	"golang.org/x/crypto/chacha20poly1305"
)

// Key material is public compatibility data, shared with the Android client's subscription_crypto/keys.json.
//
//go:embed provider_keys.json
var providerKeysJSON []byte

var providerLink = regexp.MustCompile(`(?is)^(happ|incy)://(crypt[0-9]*)/(.*)$`)

type providerKeys struct {
	HappLegacy []string          `json:"happLegacy"`
	Happ5      map[string]string `json:"happ5"`
	IncyKey    string            `json:"incyKey"`
}

// decryptProviderLink opens happ://crypt.. and incy://crypt1 links. Errors never carry the link.
func decryptProviderLink(link string) (map[string]string, error) {
	match := providerLink.FindStringSubmatch(strings.TrimSpace(link))
	if match == nil || len(match[3]) > 128*1024 {
		return nil, errors.New("Неподдерживаемая зашифрованная ссылка")
	}
	var keys providerKeys
	if err := json.Unmarshal(providerKeysJSON, &keys); err != nil {
		return nil, err
	}
	provider, format, payload := strings.ToLower(match[1]), strings.ToLower(match[2]), match[3]
	content, name, err := providerDecrypt(keys, provider, format, payload)
	// Nonce and salt bytes of crypt5 can contain a literal '%', so the raw form goes first.
	if err != nil && strings.Contains(payload, "%") {
		if decoded, e := url.QueryUnescape(strings.ReplaceAll(payload, "+", "%2B")); e == nil {
			content, name, err = providerDecrypt(keys, provider, format, decoded)
		}
	}
	if err != nil {
		return nil, errors.New("Не удалось расшифровать ссылку: формат не поддерживается или данные повреждены")
	}
	return map[string]string{"content": strings.TrimSpace(content), "name": name}, nil
}

func providerDecrypt(keys providerKeys, provider, format, payload string) (string, string, error) {
	switch {
	case provider == "incy" && format == "crypt1":
		return incyCrypt1(keys.IncyKey, payload)
	case provider == "happ" && format == "crypt5":
		text, err := happCrypt5(keys.Happ5, payload)
		return text, "", err
	case provider == "happ" && (format == "crypt" || format == "crypt2" || format == "crypt3" || format == "crypt4"):
		ordinal := 0
		if format != "crypt" {
			ordinal = int(format[len(format)-1]-'0') - 1
		}
		if ordinal >= len(keys.HappLegacy) {
			return "", "", errors.New("unknown key")
		}
		text, err := happLegacy(keys.HappLegacy[ordinal], payload)
		return text, "", err
	}
	return "", "", errors.New("unsupported format")
}

func providerBase64(text string) ([]byte, error) {
	clean := strings.Join(strings.Fields(text), "")
	clean = strings.TrimRight(strings.NewReplacer("-", "+", "_", "/").Replace(clean), "=")
	return base64.RawStdEncoding.DecodeString(clean)
}

func providerPrivateKey(encoded string) (*rsa.PrivateKey, error) {
	der, err := providerBase64(encoded)
	if err != nil {
		return nil, err
	}
	parsed, err := x509.ParsePKCS8PrivateKey(der)
	if err != nil {
		return nil, err
	}
	key, ok := parsed.(*rsa.PrivateKey)
	if !ok {
		return nil, errors.New("not an RSA key")
	}
	return key, nil
}

func incyCrypt1(encodedKey, payload string) (string, string, error) {
	wire, err := providerBase64(strings.TrimRight(payload, "/"))
	if err != nil || len(wire) <= 28 {
		return "", "", errors.New("invalid payload")
	}
	key, err := providerBase64(encodedKey)
	if err != nil {
		return "", "", err
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return "", "", err
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return "", "", err
	}
	clear, err := gcm.Open(nil, wire[:12], wire[12:], nil)
	if err != nil {
		return "", "", err
	}
	var document struct {
		V    int    `json:"v"`
		URL  string `json:"url"`
		Name string `json:"n"`
	}
	if err = json.Unmarshal(clear, &document); err != nil || document.V != 1 {
		return "", "", errors.New("invalid document")
	}
	parsed, err := url.Parse(document.URL)
	if err != nil || (parsed.Scheme != "http" && parsed.Scheme != "https") || parsed.Host == "" || parsed.User != nil {
		return "", "", errors.New("invalid address")
	}
	if len([]rune(document.Name)) > 128 {
		document.Name = string([]rune(document.Name)[:128])
	}
	return document.URL, document.Name, nil
}

func happLegacy(encodedKey, payload string) (string, error) {
	key, err := providerPrivateKey(encodedKey)
	if err != nil {
		return "", err
	}
	wire, err := providerBase64(payload)
	size := key.Size()
	if err != nil || len(wire) == 0 || len(wire)%size != 0 || len(wire)/size > 256 {
		return "", errors.New("invalid payload")
	}
	var out bytes.Buffer
	for offset := 0; offset < len(wire); offset += size {
		part, err := rsa.DecryptPKCS1v15(nil, key, wire[offset:offset+size])
		if err != nil {
			return "", err
		}
		out.Write(part)
	}
	if !utf8.Valid(out.Bytes()) {
		return "", errors.New("invalid text")
	}
	return out.String(), nil
}

func happCrypt5(keys map[string]string, payload string) (string, error) {
	for _, c := range []byte(payload) {
		if c < 33 || c > 126 {
			return "", errors.New("invalid payload")
		}
	}
	shuffled := []byte(payload)
	for offset := 0; offset+4 <= len(shuffled)-len(shuffled)%4; offset += 4 {
		shuffled[offset], shuffled[offset+1], shuffled[offset+2], shuffled[offset+3] = shuffled[offset+2], shuffled[offset+3], shuffled[offset], shuffled[offset+1]
	}
	if len(shuffled) < 21 {
		return "", errors.New("invalid payload")
	}
	marker := string(shuffled[:4]) + string(shuffled[len(shuffled)-4:])
	encoded, ok := keys[marker]
	if !ok {
		return "", errors.New("unknown key generation")
	}
	key, err := providerPrivateKey(encoded)
	if err != nil {
		return "", err
	}
	body := shuffled[4 : len(shuffled)-4]
	// Some nonce bytes resemble a legacy header; both layouts are tried, only an authenticated result counts.
	digit := body[12] >= '0' && body[12] <= '9'
	for _, salted := range []bool{!digit, digit} {
		if text, err := happCrypt5Body(body, key, salted); err == nil {
			return text, nil
		}
	}
	return "", errors.New("authentication failed")
}

func providerSwapPairs(data []byte) []byte {
	out := append([]byte{}, data...)
	for i := 0; i+1 < len(out); i += 2 {
		out[i], out[i+1] = out[i+1], out[i]
	}
	return out
}

func happCrypt5Body(body []byte, key *rsa.PrivateKey, salted bool) (string, error) {
	start := 12
	if salted {
		start = 22
	}
	if len(body) <= start {
		return "", errors.New("short body")
	}
	end := start
	for end < len(body) && body[end] >= '0' && body[end] <= '9' {
		end++
	}
	if end == start || end-start > 7 {
		return "", errors.New("invalid size")
	}
	size, err := strconv.Atoi(string(body[start:end]))
	if err != nil || size <= 0 || size >= len(body)-end-1 {
		return "", errors.New("invalid size")
	}
	sealed, err := providerBase64(string(body[end+1 : end+1+size]))
	if err != nil {
		return "", err
	}
	wrapped, err := providerBase64(string(body[end+1+size:]))
	if err != nil {
		return "", err
	}
	unwrapped, err := rsa.DecryptPKCS1v15(nil, key, wrapped)
	if err != nil {
		return "", err
	}
	secret, err := providerBase64(string(providerSwapPairs(unwrapped)))
	if err != nil || len(secret) != 32 || len(sealed) < 16 {
		return "", errors.New("invalid secret")
	}
	if salted {
		for i := range secret {
			secret[i] ^= body[14+i%8]
		}
	}
	aead, err := chacha20poly1305.New(secret)
	if err != nil {
		return "", err
	}
	clear, err := aead.Open(nil, body[:12], sealed, nil)
	if err != nil {
		return "", err
	}
	decoded, err := providerBase64(string(providerSwapPairs(clear)))
	if err != nil || !utf8.Valid(decoded) {
		return "", errors.New("invalid text")
	}
	return string(decoded), nil
}
