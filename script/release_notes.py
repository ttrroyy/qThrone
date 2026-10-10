import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Выключение TUN останавливает активный профиль qWDTT и его фоновые проверки без повторной авторизации в режиме прокси.")
print("- Завершение тестового процесса qWDTT при ошибке авторизации больше не вызывает остановку и переподключение активного профиля другого протокола.")
print("- Отмена подключения прерывает создание TURN-соединения, получение TURN allocation и ожидание конфигурации сервера. RAW-сессия отправляет отключение до удаления маршрутов TUN; завершение процесса ожидается перед запуском следующей сессии.")
print("- TUN для qWDTT использует IPv4 без IPv6-адреса интерфейса. Настройка IPv6 сохраняется и продолжает применяться к остальным протоколам.")
print()
print(f"Основа: Throne {version['throne_version']}, ветка dev, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
