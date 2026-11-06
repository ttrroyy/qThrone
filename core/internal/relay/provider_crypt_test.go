package relay

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// The vectors are the Android client's, so both clients open the same links the same way.
func TestDecryptProviderLinkVectors(t *testing.T) {
	data, err := os.ReadFile("testdata/provider_vectors.json")
	if err != nil {
		t.Fatal(err)
	}
	var vectors struct {
		Links []struct {
			Name string `json:"name"`
			Link string `json:"link"`
			Text string `json:"text"`
		} `json:"links"`
	}
	if err = json.Unmarshal(data, &vectors); err != nil {
		t.Fatal(err)
	}
	if len(vectors.Links) == 0 {
		t.Fatal("no vectors")
	}
	for _, vector := range vectors.Links {
		out, err := decryptProviderLink(vector.Link)
		// Encrypted correctly, but what they open is no subscription address: refused on purpose.
		if strings.HasPrefix(vector.Name, "incy-bad-") || vector.Name == "incy-file-url" {
			if err == nil {
				t.Errorf("%s: accepted", vector.Name)
			}
			continue
		}
		if err != nil {
			t.Errorf("%s: %v", vector.Name, err)
			continue
		}
		if out["content"] != strings.TrimSpace(vector.Text) {
			t.Errorf("%s: wrong text", vector.Name)
		}
	}
}

func TestDecryptProviderResponseVectors(t *testing.T) {
	data, err := os.ReadFile("testdata/provider_vectors.json")
	if err != nil {
		t.Fatal(err)
	}
	var vectors struct {
		Responses []struct{ Key, Body, Tag, Text string } `json:"responses"`
	}
	if err = json.Unmarshal(data, &vectors); err != nil {
		t.Fatal(err)
	}
	if len(vectors.Responses) != 90 {
		t.Fatalf("%d vectors", len(vectors.Responses))
	}
	path := filepath.Join(t.TempDir(), "body")
	for i, vector := range vectors.Responses {
		if err = os.WriteFile(path, []byte(vector.Body), 0o600); err != nil {
			t.Fatal(err)
		}
		// Every other one finds its key only in the address before the redirect.
		final, original := "https://sub.example.test/?key="+vector.Key, "https://sub.example.test/"
		if i%2 == 1 {
			final, original = original, final
		}
		if err = decryptProviderResponse(path, vector.Tag, final, original); err != nil {
			t.Fatalf("%s: %v", vector.Key, err)
		}
		if clear, _ := os.ReadFile(path); string(clear) != vector.Text {
			t.Errorf("%s: wrong text", vector.Key)
		}
	}
	if err = os.WriteFile(path, []byte(vectors.Responses[0].Body), 0o600); err != nil {
		t.Fatal(err)
	}
	if decryptProviderResponse(path, vectors.Responses[1].Tag, "https://sub.example.test/?key="+vectors.Responses[0].Key, "") == nil {
		t.Error("a wrong tag was accepted")
	}
}

func TestDecryptProviderLinkRejectsDamagedData(t *testing.T) {
	if _, err := decryptProviderLink("happ://crypt5/invalid"); err == nil || strings.Contains(err.Error(), "invalid") {
		t.Fatalf("damaged link: %v", err)
	}
}
