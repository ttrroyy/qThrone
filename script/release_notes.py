import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print(f"# qThrone {version['version']}")
print()
print("- Обновлены Xray до базы 26.10.31 и sing-box до базы 1.15.0-alpha.13 с расширениями Throne и связанными сетевыми библиотеками.")
print("- Исправлена загрузка геофайлов подписок Happ и Incy размером больше 64 МБ. Поддерживаются базы до 256 МБ; перед заменой кеша файл проверяется, при ошибке обновления сохраняется рабочая копия. Конвертер загружает в память только выбранную категорию.")
print("- Исправлено отображение HTTPS-прокси. TLS и настройки проверки сертификата сохраняются при импорте, сохранении и экспорте профиля.")
print("- Небольшие изменения и исправления интерфейса.")
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
