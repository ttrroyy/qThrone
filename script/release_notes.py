import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Исправлено отображение и сохранение выбранного выхода в правилах маршрутизации.")
print("- Сбой дополнительного qWDTT или CSQTT больше не отключает основной VPN.")
print("- Запросы через недоступный дополнительный выход завершаются ошибкой без перехода на direct.")
print()
print(f"Основа: Throne {version['throne_version']}, ветка dev, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
