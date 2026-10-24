package rpc

import "testing"

func TestQWDTTCallKeySharedAcrossServers(t *testing.T) {
	a, okA := qwdttCallKey(`{"peer":"1.1.1.1:56000","hashes":["aaaaaaaaaaaaaaaa","bbbbbbbbbbbbbbbb"]}`)
	b, okB := qwdttCallKey(`{"peer":"2.2.2.2:56000","hashes":["bbbbbbbbbbbbbbbb","aaaaaaaaaaaaaaaa"]}`)
	c, _ := qwdttCallKey(`{"peer":"1.1.1.1:56000","hashes":["cccccccccccccccc"]}`)
	if !okA || !okB || a != b || a == c {
		t.Fatal("profiles joining the same VK calls must share one key, others must not")
	}
	if _, ok := qwdttCallKey(`{"peer":"1.1.1.1:56000","hash_source":"auto"}`); ok {
		t.Fatal("automatic hashes have no shared call key")
	}
}
