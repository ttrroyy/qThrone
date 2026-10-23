import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print('- Результаты пинг-теста снова появляются по мере готовности каждого профиля, а не после завершения всей пачки. Параллельный запуск пинг-тестов разных профилей сохранён; спидтест, как и раньше, запускается только один и блокирует остальные тесты.')
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
