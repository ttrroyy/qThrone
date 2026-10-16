import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Проверка задержки qWDTT снова выполняется HTTP-запросом через туннель, включая RAW. Активное подключение используется без перезапуска; независимые профили проверяются параллельно.")
print("- При недоступном первом хеше qWDTT пробует оставшиеся хеши для запуска туннеля.")
print("- Исправлена автоматическая капча в фоновых проверках CSQTT; увеличен запас времени на подготовку транспорта без увеличения таймаута самого HTTP-замера.")
print("- Уточнены подписи IP и адреса DNS в профилях маршрутизации подписки: отдельно для прокси и прямого подключения.")
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
