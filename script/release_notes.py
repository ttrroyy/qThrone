import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print('- CSQTT: игры Valve (CS2, Dota 2 и другие) снова находят релеи Steam. Внутренний MTU моста CSQTT поднят с 1300 до 1400: полноразмерные игровые UDP-пакеты больше не режутся на IP-фрагменты, которые терялись по пути. MTU TUN в настройках по-прежнему 1500.')
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
