import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print(f"# qThrone {version['version']}")
print()
print("- В списке серверов рядом с протоколом показываются транспорт и защита: `VLESS / TCP / Reality`, `VLESS / XHTTP / TLS`, `Trojan / gRPC / TLS`. Hysteria 2 подписывается как `Hysteria 2`, а AmneziaWG с версией параметров: `AWG 1.0`, `AWG 1.5`, `AWG 2.0`, `AWG 3.0`, `AWG 3.1`. Обычный WireGuard остаётся `WireGuard`. Поиск по списку находит и по этим подписям. Если включён показ защиты в колонке типа, у таких протоколов она уже в подписи, а значок предупреждения остаётся.")
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
