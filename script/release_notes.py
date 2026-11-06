import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print(f"# qThrone {version['version']}")
print()
print("- Исправлены WS и gRPC для ссылок с ALPN, не подходящим транспорту.")
print("- HTTPS-прокси по IP без SNI теперь подключается. Проверка сертификата для такого адреса отключена, с указанным SNI она работает.")
print("- Добавлен импорт зашифрованных ссылок happ://crypt и incy://crypt1.")
print("- Появилось создание своих профилей маршрутизации Happ и Incy без подписки, а в настройках пояснено, как они работают вместе с общей маршрутизацией.")
print("- Проверка задержки по умолчанию идёт через HTTPS.")
print("- Вход VK находит больше браузеров: Chrome и Edge в нестандартных папках, Chromium, Brave и Яндекс Браузер.")
print("- Окно проверки OpenFlux больше не остаётся пустым about:blank.")
print("- Название из подписки заменяет адрес в заголовке группы.")
print("- Импорт профилей Soar из SlipNet 2.5.6.")
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
