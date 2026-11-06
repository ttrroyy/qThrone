import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print(f"# qThrone {version['version']}")
print()
print("- WS и gRPC для Trojan и VLESS теперь работают на Xray, как в других клиентах, это исправляет профили с панелей вроде 3x-ui 3.11. Учитывается параметр authority.")
print("- В профилях qWDTT, CSQTT и WDTT Plus можно выбрать хеши из созданных в клиенте.")
print("- Обновлены OpenFlux до 0.17.1 и MasterDNS.")
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
