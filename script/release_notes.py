import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Исправлены ссылки WDTT старого формата (wdtt://адрес:порт:порт_WG:локальный_порт:пароль:хеши). В 1.5 и 1.5.1 они добавлялись как «WDTT» без выбора режима RAW/WG и не подключались. Теперь они снова добавляются как qWDTT со всеми настройками.")
print("- Профили, которые 1.5 и 1.5.1 уже сохранили как «WDTT», при запуске автоматически снова становятся qWDTT. Добавлять их заново не нужно.")
print("- Ссылки WDTT Plus (wdtt://connect?v=1…) по-прежнему открываются в клиенте WDTT Plus.")
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
