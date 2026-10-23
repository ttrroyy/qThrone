import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print('- Хеши VK: в настройках VK-релея остался выбор источника для новых профилей («Вручную» или «Авто») и появилась кнопка «Генерация хешей».')
print('- В окне генерации кнопка «+» создаёт нужное количество хешей звонков VK. Кнопка «Копировать» справа копирует хеш, крестик удаляет его и завершает звонок в VK.')
print('- Если удерживать хеш, включается выделение: сверху справа появляются «Выбрать все» и «Удалить».')
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
