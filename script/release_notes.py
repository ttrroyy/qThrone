import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Исправлен нативный пинг qWDTT: авторизация VK и проверка TURN/DTLS используют отдельные ограничения времени. Недоступный сервер не вызывает длительного ожидания.")
print("- Для фоновой проверки восстановлена автоматическая обработка капчи без открытия ручного окна.")
print("- Проверка хешей использует профиль устройства подключения; уточнены сообщения об ошибках проверки.")
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
