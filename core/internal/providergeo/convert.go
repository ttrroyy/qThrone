package providergeo

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/netip"
	"os"
	"path/filepath"
	"strings"

	"github.com/sagernet/sing-box/common/srs"
	"github.com/sagernet/sing-box/option"
	"github.com/xtls/xray-core/common/geodata"
	"google.golang.org/protobuf/encoding/protowire"
	"google.golang.org/protobuf/proto"
)

// ConvertGeoRuleSet keeps the provider's own geo categories rather than substituting another database.
func Convert(input, output, kind, selector string) error {
	if kind != "geoip" && kind != "geosite" {
		return fmt.Errorf("unsupported geo kind")
	}
	if selector == "" {
		return fmt.Errorf("empty geo category")
	}
	f, err := os.Open(input)
	if err != nil {
		return err
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil {
		return err
	}
	if st.Size() > 64<<20 {
		return fmt.Errorf("geo file too large")
	}
	data, err := io.ReadAll(io.LimitReader(f, (64<<20)+1))
	if err != nil {
		return err
	}
	if len(data) > 64<<20 {
		return fmt.Errorf("geo file too large")
	}
	parts := strings.SplitN(selector, "@", 2)
	fields := map[string][]string{}
	found := false
	for len(data) > 0 {
		number, wire, n := protowire.ConsumeTag(data)
		if n < 0 {
			return protowire.ParseError(n)
		}
		data = data[n:]
		if number != 1 || wire != protowire.BytesType {
			n = protowire.ConsumeFieldValue(number, wire, data)
			if n < 0 {
				return protowire.ParseError(n)
			}
			data = data[n:]
			continue
		}
		entry, n := protowire.ConsumeBytes(data)
		if n < 0 {
			return protowire.ParseError(n)
		}
		data = data[n:]
		if kind == "geosite" {
			var site geodata.GeoSite
			if err = proto.Unmarshal(entry, &site); err != nil {
				return err
			}
			if !strings.EqualFold(site.Code, parts[0]) {
				continue
			}
			found = true
			var matcher geodata.AttributeMatcher
			if len(parts) == 2 {
				matcher = geodata.NewAllAttrsMatcher(parts[1])
			}
			for _, d := range site.Domain {
				if matcher != nil && !matcher.Match(d) {
					continue
				}
				key := ""
				switch d.Type {
				case geodata.Domain_Substr:
					key = "domain_keyword"
				case geodata.Domain_Regex:
					key = "domain_regex"
				case geodata.Domain_Domain:
					key = "domain_suffix"
				case geodata.Domain_Full:
					key = "domain"
				default:
					return fmt.Errorf("unsupported geo domain type")
				}
				fields[key] = append(fields[key], d.Value)
			}
		} else if kind == "geoip" {
			var ips geodata.GeoIP
			if err = proto.Unmarshal(entry, &ips); err != nil {
				return err
			}
			if !strings.EqualFold(ips.Code, parts[0]) {
				continue
			}
			found = true
			if ips.ReverseMatch {
				return fmt.Errorf("reverse geo category is not supported")
			}
			for _, c := range ips.Cidr {
				address, ok := netip.AddrFromSlice(c.Ip)
				if !ok || int(c.Prefix) > address.BitLen() {
					return fmt.Errorf("invalid geo CIDR")
				}
				fields["ip_cidr"] = append(fields["ip_cidr"], netip.PrefixFrom(address, int(c.Prefix)).Masked().String())
			}
		} else {
			return fmt.Errorf("unsupported geo kind")
		}
		break
	}
	if !found {
		return fmt.Errorf("geo category %s not found", selector)
	}
	if len(fields) == 0 {
		return fmt.Errorf("geo category %s is empty", selector)
	}
	b, err := json.Marshal(map[string]any{"rules": []any{fields}})
	if err != nil {
		return err
	}
	var rules option.PlainRuleSet
	if err = json.Unmarshal(b, &rules); err != nil {
		return err
	}
	var encoded bytes.Buffer
	if err = srs.Write(&encoded, rules, 3); err != nil {
		return err
	}
	temp, err := os.CreateTemp(filepath.Dir(output), ".geo-*.srs")
	if err != nil {
		return err
	}
	defer os.Remove(temp.Name())
	if _, err = temp.Write(encoded.Bytes()); err != nil {
		temp.Close()
		return err
	}
	if err = temp.Close(); err != nil {
		return err
	}
	return os.Rename(temp.Name(), output)
}
