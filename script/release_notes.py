import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print(f"# qThrone {version['version']}")
print()
print("- Если геофайлы профиля маршрутизации Happ или Incy (например, RoscomVPN) не удалось обновить при подключении или смене профиля, используется уже скачанная копия. Раньше в этом случае профиль не запускался, даже если старый туннель как раз не работал.")
print("- Ссылки Hysteria 2 с интервалом смены портов без единицы измерения (например, `hop_interval=10`) теперь работают: число считается секундами. Раньше такой интервал отбрасывался, и смена портов не включалась.")
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
