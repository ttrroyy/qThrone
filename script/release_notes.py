import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Подготовка qWDTT/CSQTT больше не занимает потоки обычных проверок. Каждый HTTP-тест имеет отдельную отмену и ограничение времени.")
print("- Ошибка запуска или отсутствие результата теста теперь отображается как недоступность, вместо пустого поля.")
print("- Подключение qWDTT/CSQTT ожидает готовности локального SOCKS-моста до запуска основного ядра.")
print("- Исправлены таймауты браузерной капчи и завершение проверки хешей; добавлено отображение этапа решения капчи.")
print("- Переработан запуск транспорта через ограниченный токен Windows при отключённом UAC. В лог добавлен код аварийного завершения процесса.")
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
