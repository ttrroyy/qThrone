package main

import (
	"context"
	"fmt"
	"log"
)

// The configuration-owning group must try the remaining links before exiting.
// Other groups retain their assigned hash and original worker distribution.
func fetchGroupCredentials(ctx context.Context, hashes []string, start, stream int, configOwner bool,
	fetch func(context.Context, string, int) (string, string, []string, error)) (string, string, []string, string, error) {
	if len(hashes) == 0 {
		return "", "", nil, "", fmt.Errorf("no VK hashes")
	}
	attempts := 1
	if configOwner {
		attempts = len(hashes)
	}
	var last error
	for i := 0; i < attempts; i++ {
		if err := ctx.Err(); err != nil {
			return "", "", nil, "", err
		}
		hash := hashes[(start+i)%len(hashes)]
		user, pass, urls, err := fetch(ctx, hash, stream)
		if err == nil {
			return user, pass, urls, hash, nil
		}
		last = err
		if i+1 < attempts {
			log.Printf("[qWDTT] VK authorization failed for hash slot %d; trying the next configured hash", (start+i)%len(hashes)+1)
		}
	}
	return "", "", nil, "", last
}
