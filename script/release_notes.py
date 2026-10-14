import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Подписки Happ и Incy импортируют отдельные профили маршрутизации, DNS и источники Geo. Настройки доступны в окне маршрутизации и в настройках группы.")
print("- Geo-файлы провайдеров загружаются автоматически и хранятся отдельно по источникам. Их категории используются без подмены общей базой.")
print("- Для Xray JSON отображаются протокол и адрес сервера. Полные конфиги сохраняют встроенные балансеры, маршрутизацию и цепочки.")
print("- Повреждённая маршрутизация не мешает обновлению серверов. Выбор источника хешей qWDTT сохраняется при обновлении подписки.")
print()
print(f"Основа: Throne {version['throne_version']}, ветка dev, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
