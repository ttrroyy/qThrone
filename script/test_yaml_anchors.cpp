#include "3rdparty/fkYAML/node.hpp"
#include <cassert>
int main() {
    const auto blocks = fkyaml::node::deserialize("base: &base\n  type: wireguard\n  port: 443\ncopy: *base\n");
    assert(blocks["copy"].is_mapping());
    assert(blocks["copy"]["type"].get_value<std::string>() == "wireguard");
    assert(blocks["copy"]["port"].get_value<int>() == 443);
    const auto scalars = fkyaml::node::deserialize("base: &base\n  value\ncopy: *base\n");
    assert(scalars["copy"].get_value<std::string>() == "value");
    const auto key = fkyaml::node::deserialize("base:\n  &key field: value\ncopy: *key\n");
    assert(key["copy"].get_value<std::string>() == "field");
    const auto seq = fkyaml::node::deserialize("base: &base\n  - value\ncopy: *base\n");
    assert(seq["copy"].is_sequence());
    assert(seq["copy"][0].get_value<std::string>() == "value");
    const auto flow = fkyaml::node::deserialize("base: &base {type: masque}\ncopy: *base\n");
    assert(flow["copy"]["type"].get_value<std::string>() == "masque");
}
