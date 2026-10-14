package relay

import (
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"github.com/zalando/go-keyring"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// Only the encryption key goes into the OS credential store; cookie sets and SSH keys can exceed its entry size limit.
func vaultKey() ([]byte, error) {
	unlock, e := vaultLock(context.Background(), "key")
	if e != nil {
		return nil, e
	}
	defer unlock()
	value, e := keyring.Get("qThrone VK relay", "vault-key")
	if e == keyring.ErrNotFound {
		b := make([]byte, 32)
		if _, e = rand.Read(b); e != nil {
			return nil, e
		}
		value = hex.EncodeToString(b)
		if e = keyring.Set("qThrone VK relay", "vault-key", value); e != nil {
			return nil, e
		}
	}
	if e != nil && e != keyring.ErrNotFound {
		return nil, e
	}
	b, e := hex.DecodeString(value)
	if e != nil || len(b) != 32 {
		return nil, fmt.Errorf("Некорректный ключ хранилища")
	}
	return b, nil
}
func vaultLock(ctx context.Context, name string) (func(), error) {
	path, e := vaultPath("account")
	if e != nil {
		return nil, e
	}
	path = filepath.Join(filepath.Dir(path), name+".lock")
	timeout := time.NewTimer(90 * time.Second)
	defer timeout.Stop()
	for {
		file, e := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0600)
		if e == nil {
			file.Close()
			return func() { _ = os.Remove(path) }, nil
		}
		if !os.IsExist(e) {
			return nil, e
		}
		if stat, e := os.Stat(path); e == nil && time.Since(stat.ModTime()) > 5*time.Minute {
			_ = os.Remove(path)
			continue
		}
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case <-timeout.C:
			return nil, fmt.Errorf("Хранилище занято другой операцией")
		case <-time.After(100 * time.Millisecond):
		}
	}
}
func vaultPath(id string) (string, error) {
	if id != "account" && id != "servers" {
		return "", fmt.Errorf("Неизвестное хранилище")
	}
	dir, e := os.UserConfigDir()
	if e != nil {
		return "", e
	}
	dir = filepath.Join(dir, "qThrone", "relay-vault")
	if e = os.MkdirAll(dir, 0700); e != nil {
		return "", e
	}
	return filepath.Join(dir, id+".aes"), nil
}
func vaultCipher() (cipher.AEAD, error) {
	key, e := vaultKey()
	if e != nil {
		return nil, e
	}
	block, e := aes.NewCipher(key)
	if e != nil {
		return nil, e
	}
	return cipher.NewGCM(block)
}
func secureGet(id string) (string, error) {
	path, e := vaultPath(id)
	if e != nil {
		return "", e
	}
	data, e := os.ReadFile(path)
	if os.IsNotExist(e) {
		return "", keyring.ErrNotFound
	}
	if e != nil {
		return "", e
	}
	a, e := vaultCipher()
	if e != nil {
		return "", e
	}
	if len(data) < a.NonceSize() {
		return "", fmt.Errorf("Повреждено хранилище")
	}
	plain, e := a.Open(nil, data[:a.NonceSize()], data[a.NonceSize():], []byte(id))
	return string(plain), e
}
func secureSet(id, value string) error {
	path, e := vaultPath(id)
	if e != nil {
		return e
	}
	a, e := vaultCipher()
	if e != nil {
		return e
	}
	nonce := make([]byte, a.NonceSize())
	if _, e = rand.Read(nonce); e != nil {
		return e
	}
	data := a.Seal(nonce, nonce, []byte(value), []byte(id))
	tmp, e := os.CreateTemp(filepath.Dir(path), ".vault-")
	if e != nil {
		return e
	}
	defer os.Remove(tmp.Name())
	if _, e = tmp.Write(data); e != nil {
		tmp.Close()
		return e
	}
	if e = tmp.Sync(); e != nil {
		tmp.Close()
		return e
	}
	if e = tmp.Close(); e != nil {
		return e
	}
	return os.Rename(tmp.Name(), path)
}
func secureDelete(id string) error {
	path, e := vaultPath(id)
	if e != nil {
		return e
	}
	e = os.Remove(path)
	if os.IsNotExist(e) {
		return nil
	}
	return e
}
func vaultError(e error) error {
	if e == nil {
		return nil
	}
	if strings.Contains(e.Error(), "secret service") {
		return fmt.Errorf("Системное хранилище секретов недоступно. Запустите GNOME Keyring или KWallet")
	}
	return e
}
