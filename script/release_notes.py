import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print('- Пинг-тест больше не зависает на отдельных серверах. Если первый запрос к серверу не ответил за секунду (например, потерялся пакет при подключении), параллельно уходит второй, и берётся тот, что ответит раньше. Измеряемый запрос не меняется, поэтому цифры пинга остаются честными.')
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
