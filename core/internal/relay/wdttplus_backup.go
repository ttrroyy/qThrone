package relay

import (
	"bytes"
	"compress/gzip"
	"crypto/aes"
	"crypto/cipher"
	"crypto/pbkdf2"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"time"
)

const wdttPlusTransferIterations = 210000

// wdttPlusPortableBackup turns the server's snapshot into WDTT Plus's portable backup
// document (format wdtt-server-backup, version 4), as its app saves a copy.
func wdttPlusPortableBackup(snapshot map[string]any, sourceHost string) (string, error) {
	if snapshot["format"] != "wdtt-server-snapshot" {
		return "", errors.New("Неподдерживаемый формат серверной копии")
	}
	files, ok := snapshot["files"].([]any)
	if !ok {
		return "", errors.New("В копии нет файлов")
	}
	data := map[string]string{}
	for _, entry := range files {
		file, _ := entry.(map[string]any)
		path, _ := file["path"].(string)
		encoded, _ := file["data_b64"].(string)
		raw, err := base64.StdEncoding.DecodeString(encoded)
		if path == "" || err != nil {
			return "", errors.New("Повреждённый файл в копии")
		}
		data[path] = string(raw)
	}
	passwords, ok := data["passwords.json"]
	if !ok {
		return "", errors.New("В копии нет passwords.json")
	}
	wgKeys, ok := data["wg-keys.dat"]
	if !ok {
		return "", errors.New("В копии нет wg-keys.dat")
	}
	b64 := func(value string) string { return base64.StdEncoding.EncodeToString([]byte(value)) }
	sha := func(value string) string { sum := sha256.Sum256([]byte(value)); return hex.EncodeToString(sum[:]) }
	var db struct {
		Passwords map[string]any `json:"passwords"`
	}
	_ = json.Unmarshal([]byte(passwords), &db)
	created, _ := snapshot["created_at"].(float64)
	deviceCount, _ := snapshot["device_count"].(float64)
	document := map[string]any{
		"format": "wdtt-server-backup", "version": 4,
		"created_at":  time.Unix(int64(created), 0).UTC().Format("2006-01-02T15:04:05Z"),
		"source_host": sourceHost, "passwords_json_b64": b64(passwords), "passwords_json_sha256": sha(passwords),
		"password_count": len(db.Passwords), "device_count": int(deviceCount), "owner_profile_source": "server",
		"wg_keys_dat_b64": b64(wgKeys), "wg_keys_dat_sha256": sha(wgKeys),
	}
	if value, ok := data["outbound-profile.env"]; ok {
		document["outbound_profile_env_b64"], document["outbound_profile_env_sha256"] = b64(value), sha(value)
	}
	if value, ok := data["backup-policy.json"]; ok {
		document["backup_policy_json_b64"], document["backup_policy_json_sha256"] = b64(value), sha(value)
	}
	encoded, err := json.MarshalIndent(document, "", "  ")
	return string(encoded), err
}

// wdttPlusEncryptTransfer is WdttTransferCodec.encryptServerBackup: PBKDF2-HMAC-SHA256
// (210000 iterations) derives an AES-256-GCM key for the gzipped document.
func wdttPlusEncryptTransfer(document, password string) (string, error) {
	if len([]rune(password)) < 8 {
		return "", errors.New("Пароль файла должен содержать не меньше 8 символов")
	}
	salt, iv := make([]byte, 16), make([]byte, 12)
	if _, err := rand.Read(salt); err != nil {
		return "", err
	}
	if _, err := rand.Read(iv); err != nil {
		return "", err
	}
	key, err := pbkdf2.Key(sha256.New, password, salt, wdttPlusTransferIterations, 32)
	if err != nil {
		return "", err
	}
	var compressed bytes.Buffer
	zip := gzip.NewWriter(&compressed)
	if _, err = zip.Write([]byte(document)); err != nil {
		return "", err
	}
	if err = zip.Close(); err != nil {
		return "", err
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return "", err
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return "", err
	}
	container, err := json.Marshal(map[string]any{
		"format": "wdtt-plus-transfer", "version": 1, "kind": "server-backup",
		"createdAt": time.Now().UnixMilli(), "sourceVersion": "qThrone",
		"kdf": "PBKDF2-HMAC-SHA256", "iterations": wdttPlusTransferIterations,
		"salt": base64.StdEncoding.EncodeToString(salt), "iv": base64.StdEncoding.EncodeToString(iv),
		"data": base64.StdEncoding.EncodeToString(gcm.Seal(nil, iv, compressed.Bytes(), nil)),
	})
	return string(container), err
}

type wdttPlusExportCall struct {
	ID       string `json:"id"`
	Password string `json:"password"`
}

func (p *protocol) wdttPlusBackupExport(s map[string]string, raw string) (any, error) {
	var call wdttPlusExportCall
	if json.Unmarshal([]byte(raw), &call) != nil || call.ID == "" {
		return nil, errors.New("Некорректный запрос к серверу")
	}
	p.secrets = append(p.secrets, call.Password)
	if len([]rune(call.Password)) < 8 {
		return nil, errors.New("Пароль файла должен содержать не меньше 8 символов")
	}
	request, _ := json.Marshal(wdttPlusAdminCall{Args: []string{"backup-export", "--id", call.ID}, Timeout: 180})
	answer, err := p.wdttPlusAdmin(s, string(request))
	if err != nil {
		return nil, err
	}
	snapshot, _ := answer.(map[string]any)["backup_document"].(map[string]any)
	if snapshot == nil {
		return nil, errors.New("Сервер не вернул копию")
	}
	document, err := wdttPlusPortableBackup(snapshot, s["host"])
	if err != nil {
		return nil, err
	}
	return wdttPlusEncryptTransfer(document, call.Password)
}
