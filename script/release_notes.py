import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print('- Повторное нажатие пинга больше не ждёт, пока закроется прошлый тест. Раньше каждый перезапуск ждал закрытия тестового ядра (Xray мог закрываться несколько секунд), и после частых нажатий профили подолгу крутились.')
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
