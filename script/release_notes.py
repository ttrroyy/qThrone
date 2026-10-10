import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Перенесены все изменения Throne 1.4.0-beta.2: исправлен вывод журналов тестовых соединений мобильного ядра, сообщения помечаются [test] и фильтруются по уровню журнала.")
print("- При ручной проверке задержки qWDTT требуемая VK капча открывается в отдельном окне браузера размером 520 × 700. После решения проверка продолжает запуск соединения; закрытие окна или отмена теста прекращает ожидание.")
print("- Интерактивные проверки qWDTT выполняются по одной, чтобы параллельные профили не открывали несколько окон капчи. Автоматические фоновые проверки остаются без окон; ошибки тестовых процессов не переподключают активный профиль.")
print()
print(f"Основа: Throne {version['throne_version']}, ветка dev, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
