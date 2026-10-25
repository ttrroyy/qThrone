package boxmain

import "testing"

func TestCheckObfuscatedWireGuardAndPhobos(t *testing.T) {
	config := `{
  "endpoints": [{"type": "wireguard", "tag": "wg", "address": ["10.8.0.4/32"],
    "private_key": "MBrnZoTdyT/LR4XpB7tElSxyVTQdXFw0tvVJOMSL/GI=",
    "peers": [{"address": "130.49.185.136", "port": 51824, "public_key": "g/G4y2XkTY5mPLMYYXXCarvyxUSHUzM1vpIYRHwwFT4=", "allowed_ips": ["0.0.0.0/0"]}],
    "obfs": {"type": "wg-obfuscator", "key": "XR0NEf8MhGAGcCpc", "masking": "media", "max_dummy": 45}}],
  "outbounds": [{"type": "phobos", "tag": "p", "server": "vpn.example.com", "server_port": 51824,
    "username": "Ab3Kq", "password": "Zx9Lm", "key": "XR0NEf8MhGAGcCpc", "masking": "tls"}],
  "route": {"final": "wg"}
}`
	if err := Check([]byte(config)); err != nil {
		t.Fatal(err)
	}
	bad := `{"endpoints": [{"type": "wireguard", "tag": "wg", "address": ["10.8.0.4/32"], "private_key": "MBrnZoTdyT/LR4XpB7tElSxyVTQdXFw0tvVJOMSL/GI=",
  "peers": [{"address": "1.2.3.4", "port": 1, "public_key": "g/G4y2XkTY5mPLMYYXXCarvyxUSHUzM1vpIYRHwwFT4=", "allowed_ips": ["0.0.0.0/0"]}],
  "obfs": {"type": "wg-obfuscator", "key": "k", "masking": "tls"}}]}`
	if Check([]byte(bad)) == nil {
		t.Fatal("TLS masking must be refused for WireGuard")
	}
}
