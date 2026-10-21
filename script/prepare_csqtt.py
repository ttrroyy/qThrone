"""Adapt CSQTT 2.5.0 Legacy's I/O; leave its transport/captcha code intact."""
from pathlib import Path
import subprocess
import sys

REVISION = "2066156119a5afe7d63e104aefdc8e0849999d03"
root = Path(sys.argv[1])
assert subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip() == REVISION
main = root / "rust-client/main.rs"
s = main.read_text(encoding="utf-8")

def replace(before, after):
    global s
    assert s.count(before) == 1, before
    s = s.replace(before, after, 1)

replace('std::env::args().map(normalize_cli_argument).collect()', r'''let mut args: Vec<String> = std::env::args().map(normalize_cli_argument).collect();
    if let Some(config) = mobile_config() { args.extend(mobile_arguments(&config)); }
    args''')
s += r'''

fn mobile_arguments(config: &serde_json::Value) -> Vec<String> {
    let mut args = Vec::new();
        for (key, flag) in [("peer", "peer"), ("listen", "listen"), ("obfs", "obfs"),
            ("captcha_mode", "captcha-mode"), ("vk_anon_path", "vk-auth-mode"),
            ("fingerprint", "fingerprint"), ("client_ids", "client-ids"),
            ("custom_turn_ips", "custom-turn-ips"), ("srtp_mode", "srtp-mode"),
            ("turn_transport", "turn-transport"), ("salt", "salt")] {
            if let Some(value) = config[key].as_str() { args.push(format!("--{flag}={value}")); }
        }
        if let Some(hashes) = config["hashes"].as_array() {
            let hashes: Vec<_> = hashes.iter().filter_map(|h| h.as_str()).collect();
            args.push(format!("--vk={}", hashes.join(",")));
        }
        for (key, flag) in [("workers", "workers"), ("generation", "gen")] {
            if let Some(value) = config[key].as_u64() { args.push(format!("--{flag}={value}")); }
        }
        if config["hash_source"].as_str() == Some("auto_js") {
            args.push("--vk-hash-mode=auto_js".to_owned());
        }
    args
}

#[cfg(test)]
mod mobile_arguments_tests {
    use super::*;
    #[test]
    fn private_config_keeps_secrets_out_of_arguments_and_preserves_modes() {
        let config = serde_json::json!({"peer":"[2001:db8::1]:46000", "password":"secret",
            "vk_access_token":"vk1.private", "hash_source":"auto_js", "vk_anon_path":"auto_js",
            "workers":100, "generation":42, "salt":"session", "turn_transport":"tcp_tls",
            "obfs":"unified", "captcha_mode":"rjs", "srtp_mode":"dtls"});
        let args = mobile_arguments(&config);
        assert!(!args.iter().any(|a| a.contains("secret") || a.contains("vk1.private")));
        let parsed = Arguments::try_parse_from(std::iter::once("client".to_owned()).chain(args)).unwrap();
        assert_eq!(parsed.peer, "[2001:db8::1]:46000");
        assert_eq!(parsed.vk_hash_mode, "auto_js");
        assert_eq!(parsed.vk_auth_mode, "auto_js");
        assert_eq!(parsed.turn_transport, "tcp_tls");
        assert_eq!(parsed.obfs, "unified");
        assert_eq!(parsed.captcha_mode, "rjs");
        assert_eq!(parsed.srtp_mode, "dtls");
        assert_eq!(parsed.workers, 100);
        assert_eq!(parsed.generation, 42);
    }
    #[test]
    fn manual_hashes_and_five_workers_survive_private_adapter() {
        let args = mobile_arguments(&serde_json::json!({"hashes":["first", "second"], "workers":5}));
        let parsed = Arguments::try_parse_from(std::iter::once("client".to_owned()).chain(args)).unwrap();
        assert_eq!(parsed.vk, "first,second");
        assert_eq!(parsed.workers, 5);
        assert_eq!(parsed.vk_hash_mode, "manual");
    }
}
'''
replace('let password = std::env::var("CSQTT_PASSWORD").unwrap_or_default();',
        '''let password = mobile_config().and_then(|c| c["password"].as_str().map(str::to_owned))
        .unwrap_or_else(|| std::env::var("CSQTT_PASSWORD").unwrap_or_default());''')
replace('let bootstrap = read_vk_js_bootstrap(&mut stdin).await?;',
        '''let bootstrap = match mobile_config().and_then(|c| c["vk_access_token"].as_str().map(str::to_owned)) {
            Some(token) if !token.is_empty() => vk_js_calls::Bootstrap { token },
            _ => read_vk_js_bootstrap(&mut stdin).await?,
        };''')
s += r'''

// Private per-launch file keeps tokens/passwords out of argv and process logs.
fn mobile_config() -> Option<serde_json::Value> {
    let path = std::env::var("CSQTT_CONFIG_FILE").ok()?;
    let bytes = std::fs::read(path).unwrap_or_else(|_| {
        eprintln!("Cannot read private CSQTT configuration"); std::process::exit(2)
    });
    Some(serde_json::from_slice(&bytes).unwrap_or_else(|_| {
        eprintln!("Invalid private CSQTT configuration"); std::process::exit(2)
    }))
}
'''
main.write_text(s, encoding="utf-8", newline="\n")

# The original app uses an abstract name; our authenticated socketpair uses an
# app-private filesystem endpoint. Peer UID is checked by prepare_private_csqtt.

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
