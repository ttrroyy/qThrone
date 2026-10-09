import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Исправлена штатная остановка qWDTT: сервер получает DISCONNECT_RAW до закрытия соединений при переподключении RAW.")
print("- Новые профили qWDTT используют RAW и TURN TCP по умолчанию; сохранённые настройки транспорта сохраняются.")
print("- Добавлен выбор авторизации через VK-звонок или капчу и способа решения капчи.")
print("- При необходимости капча открывается в отдельном окне Chrome, Edge или Chromium с временным профилем браузера.")
print("- Добавлены URL-тест задержки, проверка IP и тест скорости qWDTT через существующие тесты ядра.")
print("- Проверка активного qWDTT использует текущий туннель; временные соединения тестов завершаются штатно.")
print()
print(f"Основа: Throne {version['throne_version']}, ветка dev, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
