package relay

import (
	"bytes"
	"encoding/json"
	"fmt"
	"reflect"
	"regexp"
	"strconv"
	"strings"
	"time"
	"unicode"
	"unicode/utf8"
)

// Checks of the server's passwords.json as WDTT Plus's app makes them around an update
// (validatePasswordsDbStructure, prepareDeployDatabaseJson, validatePreservedServerState).

type wdttPlusDB = map[string]any

func wdttPlusParseDB(text string) (wdttPlusDB, error) {
	decoder := json.NewDecoder(strings.NewReader(text))
	decoder.UseNumber()
	var db wdttPlusDB
	if decoder.Decode(&db) != nil || db == nil || decoder.More() {
		return nil, fmt.Errorf("passwords.json на сервере повреждён")
	}
	return db, nil
}

// wdttPlusString is JSONObject.optString: a missing or null value is "", others are their text.
func wdttPlusString(m map[string]any, key string) string {
	switch v := m[key].(type) {
	case nil:
		return ""
	case string:
		return v
	case json.Number:
		return v.String()
	case bool:
		return strconv.FormatBool(v)
	default:
		data, _ := json.Marshal(v)
		return string(data)
	}
}

// wdttPlusLong is JSONObject.optLong: numbers and numeric strings, otherwise the fallback.
func wdttPlusLong(m map[string]any, key string, fallback int64) int64 {
	var text string
	switch v := m[key].(type) {
	case json.Number:
		text = v.String()
	case string:
		text = v
	default:
		return fallback
	}
	if n, e := strconv.ParseInt(text, 10, 64); e == nil {
		return n
	}
	if f, e := strconv.ParseFloat(text, 64); e == nil {
		return int64(f)
	}
	return fallback
}

func wdttPlusObject(m map[string]any, key string) (map[string]any, bool) {
	v, ok := m[key].(map[string]any)
	return v, ok
}

func wdttPlusArray(m map[string]any, key string) ([]any, bool) {
	v, ok := m[key].([]any)
	return v, ok
}

func wdttPlusLength(value string) int { return utf8.RuneCountInString(value) }

func wdttPlusPortsSpec(value string) bool {
	parts := strings.Split(value, ",")
	if len(parts) != 3 {
		return false
	}
	for _, part := range parts {
		if n, e := strconv.Atoi(strings.TrimSpace(part)); e != nil || n < 1 || n > 65535 {
			return false
		}
	}
	return true
}

var wdttPlusIPv4 = regexp.MustCompile(`^(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}$`)

func wdttPlusPublicHost(value string) bool {
	value = strings.TrimSpace(value)
	if value == "" || wdttPlusLength(value) > 253 || strings.ContainsAny(value, `/\:@`) {
		return false
	}
	if wdttPlusIPv4.MatchString(value) {
		return true
	}
	if strings.HasPrefix(value, ".") || strings.HasSuffix(value, ".") || strings.Contains(value, "..") {
		return false
	}
	labels := strings.Split(value, ".")
	if len(labels) < 2 {
		return false
	}
	for _, label := range labels {
		if label == "" || wdttPlusLength(label) > 63 || strings.HasPrefix(label, "-") || strings.HasSuffix(label, "-") {
			return false
		}
		for _, c := range label {
			if !unicode.IsLetter(c) && !unicode.IsDigit(c) && c != '-' {
				return false
			}
		}
	}
	return true
}

var wdttPlusDate = regexp.MustCompile(`^\d{4}-\d{2}-\d{2}$`)
var wdttPlusDeviceIP = regexp.MustCompile(`^10\.66\.66\.([2-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|250)$`)
var wdttPlusBase64Key = regexp.MustCompile(`^[A-Za-z0-9+/]{43}=$`)

type wdttPlusCheck struct{ err error }

func (c *wdttPlusCheck) require(ok bool, format string, args ...any) {
	if !ok && c.err == nil {
		c.err = fmt.Errorf("passwords.json на сервере не прошёл проверку: "+format, args...)
	}
}

func (c *wdttPlusCheck) maxLength(m map[string]any, key string, limit int, owner string) {
	c.require(wdttPlusLength(wdttPlusString(m, key)) <= limit, "%s у %s слишком длинный", key, owner)
}

func (c *wdttPlusCheck) traffic(owner string, list []any) {
	c.require(len(list) <= 500, "traffic у %s слишком большой", owner)
	for i, entry := range list {
		bucket, ok := entry.(map[string]any)
		c.require(ok, "traffic у %s должен содержать объекты", owner)
		if !ok {
			return
		}
		c.require(wdttPlusDate.MatchString(wdttPlusString(bucket, "date")), "traffic[%d] у %s должен содержать дату YYYY-MM-DD", i, owner)
		c.require(wdttPlusLong(bucket, "down_bytes", 0) >= 0 && wdttPlusLong(bucket, "up_bytes", 0) >= 0, "счётчики traffic[%d] у %s должны быть >= 0", i, owner)
	}
}

func (c *wdttPlusCheck) password(pass string, entry map[string]any) {
	owner := "пароля " + pass
	c.require(pass != "", "пустой пароль в passwords")
	c.require(wdttPlusLength(pass) <= 256, "пароль в passwords слишком длинный")
	c.maxLength(entry, "device_id", 256, owner)
	c.maxLength(entry, "label", 120, owner)
	c.maxLength(entry, "vk_hash", 512, owner)
	expires, purge := wdttPlusLong(entry, "expires_at", 0), wdttPlusLong(entry, "purge_after", 0)
	c.require(expires >= 0 && purge >= 0, "сроки у %s должны быть >= 0", owner)
	c.require(expires != 0 || purge == 0, "purge_after у бессрочного %s должен быть 0", owner)
	c.require(purge == 0 || purge >= expires, "purge_after у %s не может быть раньше expires_at", owner)
	c.require(wdttPlusLong(entry, "down_bytes", 0) >= 0 && wdttPlusLong(entry, "up_bytes", 0) >= 0, "счётчики у %s должны быть >= 0", owner)
	ports := wdttPlusString(entry, "ports")
	c.require(strings.TrimSpace(ports) == "" || wdttPlusPortsSpec(ports), "некорректные ports у %s", owner)
	if history, ok := wdttPlusArray(entry, "bind_history"); ok {
		c.require(len(history) <= 500, "bind_history у %s слишком большой", owner)
		for i, item := range history {
			event, ok := item.(map[string]any)
			c.require(ok, "bind_history у %s должен содержать объекты", owner)
			if !ok {
				return
			}
			eventOwner := fmt.Sprintf("bind_history[%d] %s", i, owner)
			for key, limit := range map[string]int{"device_id": 256, "device_name": 120, "device_ip": 64, "remote_ip": 64, "country": 32, "note": 256} {
				c.maxLength(event, key, limit, eventOwner)
			}
			status := wdttPlusString(event, "status")
			c.require(status == "active" || status == "unbound" || status == "denied_mismatch", "неизвестный status у %s", eventOwner)
			for _, key := range []string{"bound_at", "unbound_at", "event_at"} {
				c.require(wdttPlusLong(event, key, 0) >= 0, "%s у %s должен быть >= 0", key, eventOwner)
			}
		}
	}
	if traffic, ok := wdttPlusArray(entry, "traffic"); ok {
		c.traffic(owner, traffic)
	}
	if _, present := entry["traffic_imports"]; present {
		imports, ok := wdttPlusObject(entry, "traffic_imports")
		c.require(ok, "traffic_imports у %s должен быть объектом", owner)
		c.require(len(imports) <= 1000, "traffic_imports у %s слишком большой", owner)
		for id, item := range imports {
			c.require(id != "" && wdttPlusLength(id) <= 160, "некорректный operation-id в traffic_imports у %s", owner)
			imported, ok := item.(map[string]any)
			c.require(ok, "traffic_imports у %s должен содержать объекты", owner)
			if !ok {
				return
			}
			for _, key := range []string{"down_bytes", "up_bytes", "applied_at"} {
				c.require(wdttPlusLong(imported, key, 0) >= 0, "%s в traffic_imports у %s должен быть >= 0", key, owner)
			}
		}
	}
}

func (c *wdttPlusCheck) device(id string, device map[string]any) {
	owner := "устройства " + id
	c.require(id != "", "пустой ключ устройства в devices")
	c.require(wdttPlusLength(id) <= 256, "ключ устройства слишком длинный")
	if stored, ok := device["device_id"].(string); ok {
		c.require(strings.TrimSpace(stored) != "", "device_id %s пустой", owner)
	}
	ip := wdttPlusString(device, "ip")
	c.require(ip == "" || wdttPlusDeviceIP.MatchString(ip), "некорректный IP %s", owner)
	for _, key := range []string{"priv_key", "pub_key"} {
		value := wdttPlusString(device, key)
		c.require(value == "" || wdttPlusBase64Key.MatchString(value), "некорректный %s %s", key, owner)
	}
	for key, limit := range map[string]int{"name": 120, "model": 120, "manufacturer": 64, "brand": 64, "android_version": 64, "abi": 64,
		"app_version": 64, "locale": 64, "country": 64, "time_zone": 96, "remote_ip": 64} {
		c.maxLength(device, key, limit, owner)
	}
	c.require(wdttPlusLong(device, "sdk", 0) >= 0 && wdttPlusLong(device, "last_seen_at", 0) >= 0, "sdk и last_seen_at %s должны быть >= 0", owner)
}

// adminProfile is WDTT Plus's validateAdminProfile without vpn_dns_selection: its allowed ids come
// from the Android client, and the deploy neither reads nor changes that field.
func (c *wdttPlusCheck) adminProfile(profile map[string]any) {
	for key, limit := range map[string]int{"vk_hashes": 2048, "secondary_vk_hash": 512, "profile_name": 48, "sni": 253, "vpn_dns_selection": 32, "vpn_dns_custom": 64} {
		c.maxLength(profile, key, limit, "admin_profile")
	}
	if _, present := profile["workers_per_hash"]; present {
		n := wdttPlusLong(profile, "workers_per_hash", 0)
		c.require(n >= 1 && n <= 128, "workers_per_hash в admin_profile должен быть 1..128")
	}
	if _, present := profile["protocol"]; present {
		protocol := strings.ToLower(wdttPlusString(profile, "protocol"))
		c.require(protocol == "udp" || protocol == "tcp", "protocol в admin_profile должен быть udp или tcp")
	}
	if _, present := profile["listen_port"]; present {
		n := wdttPlusLong(profile, "listen_port", 0)
		c.require(n >= 1 && n <= 65535, "listen_port в admin_profile некорректен")
	}
	ports := wdttPlusString(profile, "ports")
	c.require(strings.TrimSpace(ports) == "" || wdttPlusPortsSpec(ports), "ports в admin_profile некорректны")
	c.require(wdttPlusLong(profile, "updated_at", 0) >= 0, "updated_at в admin_profile должен быть >= 0")
	if ids, ok := wdttPlusArray(profile, "device_ids"); ok {
		c.require(len(ids) <= 64, "слишком много device_ids в admin_profile")
		for _, id := range ids {
			text, _ := id.(string)
			c.require(wdttPlusLength(text) <= 256, "device_id в admin_profile слишком длинный")
		}
	}
}

// wdttPlusValidateDB is validatePasswordsDbStructure.
func wdttPlusValidateDB(db wdttPlusDB) error {
	c := &wdttPlusCheck{}
	main := wdttPlusString(db, "main_password")
	c.require(strings.TrimSpace(main) != "", "main_password пустой")
	c.require(wdttPlusLength(main) <= 256, "main_password слишком длинный")
	c.maxLength(db, "admin_id", 64, "базы")
	c.maxLength(db, "bot_token", 256, "базы")
	passwords, ok := wdttPlusObject(db, "passwords")
	c.require(ok, "passwords должен быть объектом")
	devices, ok := wdttPlusObject(db, "devices")
	c.require(ok, "devices должен быть объектом")
	c.require(len(passwords) <= 500, "больше 500 паролей")
	c.require(len(devices) <= 500, "больше 500 устройств")
	c.maxLength(db, "dns", 256, "базы")
	host := wdttPlusString(db, "public_ip")
	c.require(strings.TrimSpace(host) == "" || wdttPlusPublicHost(host), "public_ip должен быть доменом или IPv4 без http:// и без порта")
	ports := wdttPlusString(db, "default_ports")
	c.require(strings.TrimSpace(ports) == "" || wdttPlusPortsSpec(ports), "default_ports должен быть в формате DTLS,WG,TUN")
	max := wdttPlusLong(db, "max_passwords", 50)
	c.require(max >= 0 && max <= 500, "max_passwords должен быть 0..500")
	c.require(wdttPlusLong(db, "admin_down_bytes", 0) >= 0 && wdttPlusLong(db, "admin_up_bytes", 0) >= 0, "счётчики владельца должны быть >= 0")
	if traffic, ok := wdttPlusArray(db, "admin_traffic"); ok {
		c.traffic("admin_traffic", traffic)
	}
	if profile, ok := wdttPlusObject(db, "admin_profile"); ok {
		c.adminProfile(profile)
	}
	for pass, item := range passwords {
		entry, ok := item.(map[string]any)
		c.require(ok, "passwords.%s должен быть объектом", pass)
		if !ok {
			break
		}
		c.password(pass, entry)
		if id := wdttPlusString(entry, "device_id"); strings.TrimSpace(id) != "" {
			_, known := devices[id]
			c.require(known, "у пароля %s указано отсутствующее устройство %s", pass, id)
		}
	}
	for id, item := range devices {
		device, ok := item.(map[string]any)
		c.require(ok, "devices.%s должен быть объектом", id)
		if !ok {
			break
		}
		c.device(id, device)
	}
	return c.err
}

// wdttPlusValidateForPreserving is validatePasswordsDbForPreserving: a blank main password is
// recovered from the profile's one, which the update writes anyway.
func wdttPlusValidateForPreserving(db wdttPlusDB, mainPassword string) error {
	if strings.TrimSpace(wdttPlusString(db, "main_password")) != "" {
		return wdttPlusValidateDB(db)
	}
	if strings.TrimSpace(mainPassword) == "" {
		return fmt.Errorf("main_password пустой; укажите главный пароль в параметрах сервера")
	}
	copy := wdttPlusDB{}
	for k, v := range db {
		copy[k] = v
	}
	copy["main_password"] = mainPassword
	return wdttPlusValidateDB(copy)
}

// wdttPlusDNS is normalizedDeployDns with WDTT Plus's default when the profile has none.
func wdttPlusDNS(value string) string {
	var values []string
	for _, item := range strings.Split(value, ",") {
		if item = strings.TrimSpace(item); item != "" {
			values = append(values, item)
		}
	}
	if len(values) == 0 {
		return "1.1.1.1"
	}
	return strings.Join(values, ",")
}

// wdttPlusDatabase is prepareDeployDatabaseJson: a fresh server's passwords.json, or the current
// one with the profile's main password, DNS and ports, everything else kept as it is.
func wdttPlusDatabase(current wdttPlusDB, s map[string]string) ([]byte, error) {
	db := wdttPlusDB{}
	if current == nil {
		db = wdttPlusDB{"main_password": s["password"], "admin_id": "", "bot_token": "", "passwords": map[string]any{}, "devices": map[string]any{}}
	} else {
		for k, v := range current {
			db[k] = v
		}
	}
	if strings.TrimSpace(s["password"]) != "" {
		db["main_password"] = s["password"]
	}
	db["dns"] = wdttPlusDNS(s["dns"])
	db["default_ports"] = s["peerPort"] + "," + s["wgPort"] + "," + s["localPort"]
	if wdttPlusLong(db, "max_passwords", 0) <= 0 {
		db["max_passwords"] = 50
	}
	if e := wdttPlusValidateDB(db); e != nil {
		return nil, e
	}
	var b bytes.Buffer
	encoder := json.NewEncoder(&b)
	encoder.SetEscapeHTML(false)
	encoder.SetIndent("", "  ")
	if e := encoder.Encode(db); e != nil {
		return nil, e
	}
	return b.Bytes(), nil
}

type wdttPlusCounter struct{ down, up int64 }

func wdttPlusTrafficByDate(list []any) map[string]wdttPlusCounter {
	result := map[string]wdttPlusCounter{}
	for _, item := range list {
		bucket, ok := item.(map[string]any)
		if !ok {
			continue
		}
		date := wdttPlusString(bucket, "date")
		previous := result[date]
		result[date] = wdttPlusCounter{previous.down + wdttPlusLong(bucket, "down_bytes", 0), previous.up + wdttPlusLong(bucket, "up_bytes", 0)}
	}
	return result
}

func wdttPlusTrafficKept(before, after []any) bool {
	actual := wdttPlusTrafficByDate(after)
	for date, expected := range wdttPlusTrafficByDate(before) {
		got, ok := actual[date]
		if !ok || got.down < expected.down || got.up < expected.up {
			return false
		}
	}
	return true
}

// wdttPlusBindHistoryKept: the server keeps the last 50 events, so new ones may push out only the
// beginning; the old history's tail must then start the new one.
func wdttPlusBindHistoryKept(before, after []any) bool {
	if len(before) == 0 {
		return true
	}
	if len(after) >= len(before) && reflect.DeepEqual(after[:len(before)], before) {
		return true
	}
	if len(after) == 50 {
		for overlap := min(len(before), len(after)); overlap >= 1; overlap-- {
			if reflect.DeepEqual(before[len(before)-overlap:], after[:overlap]) {
				return true
			}
		}
	}
	return false
}

// wdttPlusValidatePreserved is validatePreservedServerState: every client still valid five minutes
// from now is kept with its binding, terms, status, counters and history.
func wdttPlusValidatePreserved(before, after wdttPlusDB, now time.Time) error {
	if e := wdttPlusValidateDB(after); e != nil {
		return e
	}
	beforePasswords, _ := wdttPlusObject(before, "passwords")
	afterPasswords, _ := wdttPlusObject(after, "passwords")
	afterDevices, _ := wdttPlusObject(after, "devices")
	cutoff := now.Unix() + 300
	for pass, item := range beforePasswords {
		old, ok := item.(map[string]any)
		if !ok {
			continue
		}
		if expires := wdttPlusLong(old, "expires_at", 0); expires != 0 && max(expires, wdttPlusLong(old, "purge_after", 0)) <= cutoff {
			continue
		}
		short := pass
		if r := []rune(pass); len(r) > 4 {
			short = string(r[:4])
		}
		fail := func(what string) error { return fmt.Errorf("после обновления %s у клиента %s…", what, short) }
		updated, ok := afterPasswords[pass].(map[string]any)
		if !ok {
			return fmt.Errorf("после обновления пропал действующий клиент %s…", short)
		}
		oldDevice, newDevice := wdttPlusString(old, "device_id"), wdttPlusString(updated, "device_id")
		if strings.TrimSpace(oldDevice) != "" {
			if newDevice != oldDevice {
				return fail("удалена или заменена привязка устройства")
			}
			if _, known := afterDevices[oldDevice]; !known {
				return fail("пропало привязанное устройство")
			}
		} else if strings.TrimSpace(newDevice) != "" {
			if _, known := afterDevices[newDevice]; !known {
				return fail("привязка ссылается на отсутствующее устройство")
			}
		}
		for _, key := range []string{"label", "vk_hash", "ports"} {
			if wdttPlusString(old, key) != wdttPlusString(updated, key) {
				return fail("изменилось поле " + key)
			}
		}
		for _, key := range []string{"expires_at", "purge_after"} {
			if wdttPlusLong(old, key, 0) != wdttPlusLong(updated, key, 0) {
				return fail("изменился срок " + key)
			}
		}
		if (old["is_deactivated"] == true) != (updated["is_deactivated"] == true) {
			return fail("изменился статус")
		}
		for _, key := range []string{"down_bytes", "up_bytes"} {
			if wdttPlusLong(updated, key, 0) < wdttPlusLong(old, key, 0) {
				return fail("уменьшился счётчик " + key)
			}
		}
		oldTraffic, _ := wdttPlusArray(old, "traffic")
		newTraffic, _ := wdttPlusArray(updated, "traffic")
		if !wdttPlusTrafficKept(oldTraffic, newTraffic) {
			return fail("уменьшилась или пропала история traffic")
		}
		oldHistory, _ := wdttPlusArray(old, "bind_history")
		newHistory, _ := wdttPlusArray(updated, "bind_history")
		if !wdttPlusBindHistoryKept(oldHistory, newHistory) {
			return fail("изменена или потеряна история bind_history")
		}
		oldImports, hadImports := wdttPlusObject(old, "traffic_imports")
		newImports, hasImports := wdttPlusObject(updated, "traffic_imports")
		if hadImports != hasImports || !reflect.DeepEqual(oldImports, newImports) {
			return fail("изменилась история учёта перенесённого трафика")
		}
	}
	for _, key := range []string{"admin_down_bytes", "admin_up_bytes"} {
		if wdttPlusLong(after, key, 0) < wdttPlusLong(before, key, 0) {
			return fmt.Errorf("после обновления уменьшилось поле %s владельца", key)
		}
	}
	oldTraffic, _ := wdttPlusArray(before, "admin_traffic")
	newTraffic, _ := wdttPlusArray(after, "admin_traffic")
	if !wdttPlusTrafficKept(oldTraffic, newTraffic) {
		return fmt.Errorf("после обновления уменьшилась или пропала история трафика владельца")
	}
	return nil
}
