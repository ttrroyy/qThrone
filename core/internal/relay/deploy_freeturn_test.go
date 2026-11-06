package relay

import (
	"slices"
	"testing"
)

func TestFreeturnAnswerReadsTheLastJsonLine(t *testing.T) {
	data, err := freeturnAnswer("noise\n{\"proto\":3,\"result\":\"ok\",\"data\":{\"obf_key\":\"k\"},\"logs\":[]}\n")
	if err != nil || data["obf_key"] != "k" {
		t.Fatalf("%v %v", data, err)
	}
	if _, err := freeturnAnswer(`{"proto":3,"result":"err","code":"needs_root","msg":"нужен root","stage":"apply","logs":[]}`); err == nil || err.Error() != "нужен root" {
		t.Fatalf("refusal: %v", err)
	}
	if _, err := freeturnAnswer("sudo: a password is required"); err == nil {
		t.Fatal("an answer without JSON must fail")
	}
}

func TestFreeturnApplyArguments(t *testing.T) {
	own := freeturnApplyArguments(map[string]string{"host": "192.0.2.1", "peerPort": "56000", "wgPort": "51820"})
	for _, want := range []string{"apply", "--method=docker", "--backend=new", "--host=192.0.2.1", "--listen-port=56000", "--wg-port=51820", "--obf-profile=rtpopus3", "--version=" + freeturnVersion} {
		if !slices.Contains(own, want) {
			t.Fatalf("%q missing from %v", want, own)
		}
	}
	external := freeturnApplyArguments(map[string]string{"host": "h", "peerPort": "1", "ftBackend": "external", "ftConnect": "127.0.0.1:443", "ftMode": "tcp"})
	if !slices.Contains(external, "--connect=127.0.0.1:443") || !slices.Contains(external, "--mode=tcp") || slices.Contains(external, "--wg-port=") {
		t.Fatalf("%v", external)
	}
}
