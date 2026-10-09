import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Исправлена выдача IPv6-адресов через DNS при подключении qWDTT: мост RAW/WG поддерживает IPv4, и неподдерживаемые AAAA-ответы больше не предлагаются приложениям в TUN.")
print("- Проверки задержки и IP независимых qWDTT-профилей выполняются параллельно и не задерживают запуск проверок других протоколов.")
print("- Результат каждого теста qWDTT сразу обновляет соответствующий профиль и счётчик завершённых проверок; результаты одинаковых тегов разных тестов изолированы.")
print("- Тесты дубликатов с одним сервером и Device ID остаются последовательными, чтобы независимые клиентские стеки не использовали одновременно один туннельный IP.")
print()
print(f"Основа: Throne {version['throne_version']}, ветка dev, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
