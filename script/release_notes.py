import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Ручные хеши задаются только в профилях qWDTT. Окна настроек VK и входа стали компактнее.")
print("- Поля SSH показываются по выбранному способу входа. Служебные параметры деплоя скрыты.")
print("- VPS добавляется кнопкой «Сохранить», а история консоли сохраняется при закрытии окна. Кнопка остановки перенесена под консоль.")
print("- Закрытие входа VK больше не отображается как ошибка. Исправлена повторная загрузка файлов деплоя с macOS.")
print()
print(f"Основа: Throne {version['throne_version']}, ветка dev, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
