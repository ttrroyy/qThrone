package wire

import (
	"fmt"
	"testing"
)

func sstpPayload(n int) []byte {
	p := make([]byte, n)
	for i := range p {
		p[i] = byte(i)
	}
	return p
}

var sstpSizes = []int{64, 576, 1400}

// BenchmarkEncodeData measures wrapping a PPP payload in an SSTP data packet. SSTP
// runs over TLS, so this 4-octet header prepend is the veepin-specific per-packet
// cost.
func BenchmarkEncodeData(b *testing.B) {
	for _, size := range sstpSizes {
		b.Run(fmt.Sprintf("%d", size), func(b *testing.B) {
			payload := sstpPayload(size)
			b.SetBytes(int64(size))
			b.ReportAllocs()
			b.ResetTimer()
			for i := 0; i < b.N; i++ {
				if _, err := EncodeData(payload); err != nil {
					b.Fatal(err)
				}
			}
		})
	}
}

// TestFramingAllocations pins EncodeData to a single allocation (the framed packet).
func TestFramingAllocations(t *testing.T) {
	payload := sstpPayload(1400)
	if n := testing.AllocsPerRun(100, func() {
		if _, err := EncodeData(payload); err != nil {
			t.Fatal(err)
		}
	}); n > 1 {
		t.Errorf("EncodeData allocates %.0f times, want 1", n)
	}
}
