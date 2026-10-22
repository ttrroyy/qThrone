package wire

import (
	"bytes"
	"encoding/binary"
	"testing"
)

func TestDataPacketRoundTrip(t *testing.T) {
	payload := []byte{0x00, 0x21, 0x45, 0x00, 0x00, 0x14}
	pkt, err := EncodeData(payload)
	if err != nil {
		t.Fatal(err)
	}
	control, body, err := ReadPacket(bytes.NewReader(pkt))
	if err != nil {
		t.Fatal(err)
	}
	if control {
		t.Error("data packet marked as control")
	}
	if !bytes.Equal(body, payload) {
		t.Errorf("body = %x, want %x", body, payload)
	}
}

func TestControlPacketRoundTrip(t *testing.T) {
	attrs := []Attribute{
		{ID: AttrEncapsulatedProtocolID, Value: []byte{0x00, 0x01}},
	}
	pkt, err := EncodeControl(MsgCallConnectRequest, attrs)
	if err != nil {
		t.Fatal(err)
	}
	control, body, err := ReadPacket(bytes.NewReader(pkt))
	if err != nil {
		t.Fatal(err)
	}
	if !control {
		t.Fatal("expected control packet")
	}
	msg, err := ParseControl(body)
	if err != nil {
		t.Fatal(err)
	}
	if msg.Type != MsgCallConnectRequest {
		t.Errorf("type = %#x, want %#x", msg.Type, MsgCallConnectRequest)
	}
	if len(msg.Attributes) != 1 {
		t.Fatalf("got %d attributes, want 1", len(msg.Attributes))
	}
	a := msg.Attributes[0]
	if a.ID != AttrEncapsulatedProtocolID {
		t.Errorf("attr ID = %#x, want %#x", a.ID, AttrEncapsulatedProtocolID)
	}
	if len(a.Value) != 2 || a.Value[0] != 0x00 || a.Value[1] != 0x01 {
		t.Errorf("attr value = %x, want 0001", a.Value)
	}
}

func TestMultipleAttributes(t *testing.T) {
	attrs := []Attribute{
		{ID: AttrEncapsulatedProtocolID, Value: []byte{0x00, 0x01}},
		{ID: AttrCryptoBindingReq, Value: []byte{0x02, 0x02, 0x00, 0x00}},
	}
	pkt, err := EncodeControl(MsgCallConnectRequest, attrs)
	if err != nil {
		t.Fatal(err)
	}
	_, body, err := ReadPacket(bytes.NewReader(pkt))
	if err != nil {
		t.Fatal(err)
	}
	msg, err := ParseControl(body)
	if err != nil {
		t.Fatal(err)
	}
	if len(msg.Attributes) != 2 {
		t.Fatalf("got %d attributes, want 2", len(msg.Attributes))
	}
	if msg.Attributes[0].ID != AttrEncapsulatedProtocolID {
		t.Errorf("first attr = %#x, want %#x", msg.Attributes[0].ID, AttrEncapsulatedProtocolID)
	}
	if msg.Attributes[1].ID != AttrCryptoBindingReq {
		t.Errorf("second attr = %#x, want %#x", msg.Attributes[1].ID, AttrCryptoBindingReq)
	}
}

func TestCryptoBindingAttribute(t *testing.T) {
	nonce := make([]byte, NonceLen+1)
	for i := range nonce[:NonceLen] {
		nonce[i] = byte(i)
	}
	certHash := make([]byte, CertHashLen+1)
	for i := range certHash[:CertHashLen] {
		certHash[i] = byte(255 - i)
	}
	compoundMAC := make([]byte, CompoundMACLen+1)
	for i := range compoundMAC[:CompoundMACLen] {
		compoundMAC[i] = 0xaa
	}

	val := make([]byte, 0, 1+1+2+NonceLen+CertHashLen+CompoundMACLen)
	val = append(val, 0x02)
	val = append(val, CertHashSHA256)
	val = append(val, 0, 0)
	val = append(val, nonce[:NonceLen]...)
	val = append(val, certHash[:CertHashLen]...)
	val = append(val, compoundMAC[:CompoundMACLen]...)

	attrs := []Attribute{{ID: AttrCryptoBinding, Value: val}}
	pkt, err := EncodeControl(MsgCallConnected, attrs)
	if err != nil {
		t.Fatal(err)
	}
	_, body, err := ReadPacket(bytes.NewReader(pkt))
	if err != nil {
		t.Fatal(err)
	}
	msg, err := ParseControl(body)
	if err != nil {
		t.Fatal(err)
	}
	if msg.Type != MsgCallConnected {
		t.Errorf("type = %#x, want %#x", msg.Type, MsgCallConnected)
	}
	a, ok := msg.Attribute(AttrCryptoBinding)
	if !ok {
		t.Fatal("expected crypto binding attribute")
	}
	if len(a.Value) != 100 {
		t.Fatalf("crypto binding value len = %d, want 100", len(a.Value))
	}
	if a.Value[0] != 0x02 {
		t.Errorf("version = %#x, want 2", a.Value[0])
	}
	if a.Value[1] != CertHashSHA256 {
		t.Errorf("hash type = %#x, want %#x", a.Value[1], CertHashSHA256)
	}
	if !bytes.Equal(a.Value[4:36], nonce[:NonceLen]) {
		t.Error("nonce mismatch")
	}
	if !bytes.Equal(a.Value[36:68], certHash[:CertHashLen]) {
		t.Error("cert hash mismatch")
	}
	if !bytes.Equal(a.Value[68:100], compoundMAC[:CompoundMACLen]) {
		t.Error("compound MAC mismatch")
	}
}

func TestAttributeMethod(t *testing.T) {
	msg := ControlMessage{
		Type: MsgCallConnectAck,
		Attributes: []Attribute{
			{ID: AttrNoError, Value: []byte{0, 0, 0, 0}},
			{ID: AttrEncapsulatedProtocolID, Value: []byte{0, 1}},
		},
	}
	a, ok := msg.Attribute(AttrEncapsulatedProtocolID)
	if !ok {
		t.Fatal("expected to find attribute")
	}
	if a.ID != AttrEncapsulatedProtocolID {
		t.Errorf("id = %#x", a.ID)
	}
	_, ok = msg.Attribute(AttrCryptoBinding)
	if ok {
		t.Error("unexpectedly found crypto binding attr")
	}
}

func TestMalformedPackets(t *testing.T) {
	t.Run("short header", func(t *testing.T) {
		_, _, err := ReadPacket(bytes.NewReader([]byte{Version}))
		if err == nil {
			t.Error("expected error for short header")
		}
	})
	t.Run("bad version", func(t *testing.T) {
		_, _, err := ReadPacket(bytes.NewReader([]byte{0xff, 0, 0, 4, 0}))
		if err == nil {
			t.Error("expected error for bad version")
		}
	})
	t.Run("length below header", func(t *testing.T) {
		_, _, err := ReadPacket(bytes.NewReader([]byte{Version, 0, 0, 2}))
		if err == nil {
			t.Error("expected error for short length")
		}
	})

	if _, err := EncodeData(make([]byte, 0x1000)); err != ErrTooLong {
		t.Error("expected ErrTooLong for oversized payload")
	}

	if _, err := ParseControl([]byte{0, 1}); err != ErrMalformed {
		t.Error("expected ErrMalformed for short control body")
	}

	// Malformed attribute inside a well-formed control body.
	body := make([]byte, 6)
	binary.BigEndian.PutUint16(body[0:2], MsgCallConnectRequest)
	binary.BigEndian.PutUint16(body[2:4], 1) // 1 attribute
	binary.BigEndian.PutUint16(body[4:6], 1) // attribute length=1 (<4)
	if _, err := ParseControl(body); err != ErrMalformed {
		t.Error("expected ErrMalformed for short attribute")
	}
}
