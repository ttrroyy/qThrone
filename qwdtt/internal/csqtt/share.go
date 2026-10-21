package csqtt

// CSQTT 2.5.0 Legacy UiUtils.kt, ttrroyy/csqtt-rebirth 2066156
// (PolyForm Noncommercial 1.0.0): nonce || AES-256-GCM ciphertext || tag.
import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base64"
	"errors"
	"strings"
	"unicode/utf8"
)

func SharePayload(value string, encrypt bool) (string, error) {
	invalid := errors.New("invalid CSQTT Legacy share payload")
	if len(value) == 0 || len(value) > 65536 {
		return "", invalid
	}
	key := sha256.Sum256([]byte("csqtt-chiper-key"))
	block, err := aes.NewCipher(key[:])
	if err != nil {
		return "", err
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return "", err
	}
	if encrypt {
		mac := hmac.New(sha256.New, key[:])
		mac.Write([]byte(value))
		nonce := mac.Sum(nil)[:gcm.NonceSize()]
		return "csqtt://" + base64.RawURLEncoding.EncodeToString(gcm.Seal(append([]byte{}, nonce...), nonce, []byte(value), nil)), nil
	}
	if !strings.HasPrefix(strings.ToLower(value), "csqtt://") {
		return "", invalid
	}
	payload := strings.NewReplacer("\r", "", "\n", "", " ", "").Replace(value[8:])
	var data []byte
	for _, encoding := range []*base64.Encoding{base64.RawURLEncoding, base64.URLEncoding, base64.RawStdEncoding, base64.StdEncoding} {
		data, err = encoding.Strict().DecodeString(payload)
		if err == nil {
			break
		}
	}
	if err != nil || len(data) < gcm.NonceSize()+gcm.Overhead() {
		return "", invalid
	}
	plain, err := gcm.Open(nil, data[:gcm.NonceSize()], data[gcm.NonceSize():], nil)
	if err != nil || !utf8.Valid(plain) {
		return "", invalid
	}
	return string(plain), nil
}
