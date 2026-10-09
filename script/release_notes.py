import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Исправлен импорт ссылок и подписок qWDTT с более чем четырьмя хешами: сохраняются первые четыре.")
print("- Добавлен выбор потоков из списка с шагом 9 и пределом 27/54/81/108 в зависимости от числа хешей.")
print("- Количество потоков из импортированных профилей приводится к допустимому диапазону вместо отказа в запуске.")
print("- Восстановлена доступность проверки обновлений после переименования обновлятора.")
print("- Исправлено удаление временного файла обновлятора после перезапуска.")
print("- Добавлена проверка обновления первой беты с сохранением RAW/WG-профилей, подписок, маршрутов и настроек.")
print()
print(f"Основа: Throne {version['throne_version']}, ветка dev, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
