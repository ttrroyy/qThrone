package main

import (
	"fmt"
	"os"
)

func recvTunFD(string) (*os.File, error) {
	return nil, fmt.Errorf("Android FD mode is unavailable on Windows; use -config")
}
