import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print('- Конфиги WARP с AmneziaWG, где IPv4 и IPv6 записаны отдельными строками Address, снова работают. Раньше оставался только последний адрес, и подключение не поднималось.')
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
