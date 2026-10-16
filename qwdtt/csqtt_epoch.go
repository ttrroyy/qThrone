package main

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"math"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"
)

// The server keeps one epoch (generation + salt) per device: a launch with a new pair
// replaces the device's older sessions, a DISCONNECT ends every session of its epoch.
// Launches that all went out as 0/"" shared the connected tunnel's epoch.
var csqttEpochMu sync.Mutex

// nextCSQTTGeneration is nextTunnelGenerationId of the original app: never below the
// clock, always above the last one, across restarts too.
func nextCSQTTGeneration(now time.Time, last uint64) uint64 {
	next := uint64(max(now.Unix(), 0))
	if last != math.MaxUint64 && last+1 > next {
		next = last + 1
	}
	return next
}

func reserveCSQTTGeneration(state string, now time.Time) (uint64, error) {
	csqttEpochMu.Lock()
	defer csqttEpochMu.Unlock()
	file := filepath.Join(state, "generation")
	var last uint64
	if data, err := os.ReadFile(file); err == nil {
		last, _ = strconv.ParseUint(strings.TrimSpace(string(data)), 10, 64)
	}
	next := nextCSQTTGeneration(now, last)
	if err := os.WriteFile(file, []byte(strconv.FormatUint(next, 10)), 0600); err != nil {
		return 0, errors.New("cannot store the CSQTT session generation")
	}
	return next, nil
}

func newCSQTTSalt() (string, error) {
	salt := make([]byte, 16)
	if _, err := rand.Read(salt); err != nil {
		return "", err
	}
	return hex.EncodeToString(salt), nil
}

// csqttLaunchConfig writes this launch's configuration, with its own epoch, into the
// device's private state folder. The caller removes it once the transport has stopped.
func csqttLaunchConfig(path, state string, now time.Time) (string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return "", errors.New("cannot read private CSQTT configuration")
	}
	var config map[string]json.RawMessage
	if json.Unmarshal(data, &config) != nil || config == nil {
		return "", errors.New("invalid CSQTT configuration")
	}
	generation, err := reserveCSQTTGeneration(state, now)
	if err != nil {
		return "", err
	}
	salt, err := newCSQTTSalt()
	if err != nil {
		return "", err
	}
	config["generation"] = json.RawMessage(strconv.FormatUint(generation, 10))
	config["salt"], _ = json.Marshal(salt)
	encoded, err := json.Marshal(config)
	if err != nil {
		return "", err
	}
	// Leftovers of a launch that was killed hold credentials: never keep them around.
	if stale, _ := filepath.Glob(filepath.Join(state, "launch-*.json")); len(stale) > 0 {
		for _, file := range stale {
			if info, err := os.Stat(file); err == nil && time.Since(info.ModTime()) > time.Hour {
				_ = os.Remove(file)
			}
		}
	}
	file, err := os.CreateTemp(state, "launch-*.json")
	if err != nil {
		return "", errors.New("cannot create private CSQTT launch configuration")
	}
	name := file.Name()
	_ = file.Chmod(0600)
	if _, err = file.Write(encoded); err != nil {
		file.Close()
		os.Remove(name)
		return "", errors.New("cannot write private CSQTT launch configuration")
	}
	if err = file.Close(); err != nil {
		os.Remove(name)
		return "", err
	}
	return name, nil
}
