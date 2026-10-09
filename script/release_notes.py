import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Исправлен импорт ссылок qWDTT с кодированием параметров Android-клиента.")
print("- Добавлено чтение названия, описания и лимита трафика подписок qWDTT.")
print("- В редакторе используется тип qWDTT; режим RAW/WG отображается в списке профилей.")
print("- Обновлены имена приложения, ядра, пакетов и системных компонентов.")
print("- Добавлен обновлятор пакетов qThrone с проверкой архива и возвратом файлов при ошибке замены.")
print("- Изменены цвета значка для обычного режима, системного прокси и TUN.")
print("- Адрес теста задержки по умолчанию: http://www.google.com/generate_204.")
print("- Исправлен путь запуска упакованного ядра qThroneCore.")
print("- Добавлена проверка запуска ядра и подключения к программе перед публикацией пакетов.")
print("- Исправлена проверка обновлений для версий 1.0-beta.N и перехода на стабильную 1.0.")
print("- Исправлен порядок подписи macOS-пакетов; добавлена проверка итоговой подписи.")
print()
print(f"Основа: Throne {version['throne_version']}, ветка dev, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
