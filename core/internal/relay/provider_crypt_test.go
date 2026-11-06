package relay

import (
	"encoding/json"
	"os"
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

func TestDecryptProviderLinkRejectsDamagedData(t *testing.T) {
	if _, err := decryptProviderLink("happ://crypt5/invalid"); err == nil || strings.Contains(err.Error(), "invalid") {
		t.Fatalf("damaged link: %v", err)
	}
}
