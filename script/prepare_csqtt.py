#!/usr/bin/env python3
"""Apply desktop I/O changes to a pinned, separately licensed CSQTT checkout."""
import pathlib
import subprocess
import sys

REVISION = "71712b07dbabe16fa43f5a248dc69e1ab9f3a270"
root = pathlib.Path(sys.argv[1]).resolve()
actual = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
if actual != REVISION:
    raise SystemExit("Unexpected CSQTT source revision")

main = root / "rust-client/main.rs"
source = main.read_text(encoding="utf-8")
old = "std::env::args().map(normalize_cli_argument).collect()"
new = r'''let mut args: Vec<String> = std::env::args().map(normalize_cli_argument).collect();
    if let Ok(path) = std::env::var("CSQTT_CONFIG_FILE") {
        let config: serde_json::Value = std::fs::read(path)
            .ok().and_then(|bytes| serde_json::from_slice(&bytes).ok())
            .unwrap_or_else(|| { eprintln!("Invalid private CSQTT configuration"); std::process::exit(2) });
        for (key, flag) in [("peer", "peer"), ("password", "password"),
            ("listen", "listen"), ("device_id", "device-id"),
            ("obfs", "obfs"), ("captcha_mode", "captcha-mode"),
            ("vk_anon_path", "vk-auth-mode")] {
            if let Some(value) = config[key].as_str() {
                args.extend([format!("--{flag}"), value.to_owned()]);
            }
        }
        if let Some(hashes) = config["hashes"].as_array() {
            let values: Vec<&str> = hashes.iter().filter_map(|hash| hash.as_str()).collect();
            args.extend(["--vk".to_owned(), values.join(",")]);
        }
        if let Some(workers) = config["workers"].as_u64() {
            args.extend(["--workers".to_owned(), workers.to_string()]);
        }
        args.extend(["--turn-transport".to_owned(),
            if config["turn_tcp"].as_bool().unwrap_or(false) { "tcp" } else { "udp" }.to_owned()]);
    }
    args'''
if source.count(old) != 1:
    raise SystemExit("CSQTT argument adapter does not match pinned source")
main.write_text(source.replace(old, new), encoding="utf-8", newline="\n")

# Abstract Unix sockets are Linux/Android-only. Desktop uses the original UDP
# dispatcher on all platforms, including macOS; no transport code is changed.
tun = root / "rust-client/tun.rs"
source = tun.read_text(encoding="utf-8")
source = source.replace('#[cfg(unix)]', '#[cfg(any(target_os = "linux", target_os = "android"))]')
source = source.replace('#[cfg(not(unix))]', '#[cfg(not(any(target_os = "linux", target_os = "android")))]')
source = source.replace('#[cfg(all(test, unix))]', '#[cfg(all(test, any(target_os = "linux", target_os = "android")))]')
tun.write_text(source, encoding="utf-8", newline="\n")

# Upstream FD integration fixtures also use Linux abstract socket addresses.
# Keep those tests on Linux; UDP transport tests still run on every platform.
dispatcher = root / "rust-client/dispatcher.rs"
source = dispatcher.read_text(encoding="utf-8")
marker = "#[cfg(test)]\nmod tests {"
before, separator, tests = source.partition(marker)
if not separator:
    raise SystemExit("CSQTT dispatcher test boundary changed")
tests = tests.replace('#[cfg(unix)]', '#[cfg(any(target_os = "linux", target_os = "android"))]')
dispatcher.write_text(before + separator + tests, encoding="utf-8", newline="\n")
integration = root / "rust-client/turn_integration_tests.rs"
source = integration.read_text(encoding="utf-8").replace('#[cfg(unix)]', '#[cfg(any(target_os = "linux", target_os = "android"))]')
integration.write_text(source, encoding="utf-8", newline="\n")
