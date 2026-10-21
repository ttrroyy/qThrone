package main

import (
	"fmt"
	"io"
	"os"
	"wg-turn-client/internal/csqtt"
)

func csqttShare(encrypt bool) error {
	data, err := io.ReadAll(io.LimitReader(os.Stdin, 65537))
	if err != nil {
		return err
	}
	value, err := csqtt.SharePayload(string(data), encrypt)
	if err != nil {
		return err
	}
	_, err = fmt.Print(value)
	return err
}
