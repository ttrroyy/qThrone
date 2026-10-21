import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print('- CSQTT переведён на оригинальный 2.5.0 Legacy из csqtt-rebirth. Старые транспортные клиенты удалены; деплой использует проверенный APK Legacy.')
print('- Добавлены зашифрованные ссылки и HTML-подписки CSQTT, Auto JS с токеном из общей привязки qWDTT, режимы SRTP и TURN UDP/TCP/TLS, 5–100 потоков.')
print('- Дополнены настройки оригинального клиента WDTT Plus: внешний TURN SNI, TCP-only, резерв WARP MASQUE и первая регистрация через сохранённый VPS. Нативная капча, подключение и установщик сохранены.')
print('- Исправлен общий путь Auto-хешей VK для qWDTT, CSQTT и WDTT Plus: повторное получение отклонённого токена, HTTP/браузерный резерв и общий источник для новых импортированных профилей.')
print('- Обновление подписки сохраняет вручную изменённые хеши, число потоков, источник хешей и режимы транспорта. Потоки не увеличиваются автоматически; снижение возможно только при превышении лимита протокола.')
print('- Добавлены импорт/экспорт masque:// с ключами %2B/%2F, HTTP/2, SNI, IPv4/IPv6 и импорт Clash YAML с общими шаблонами WireGuard/AmneziaWG и MASQUE из файла, буфера и подписки.')
print('- Добавлены ссылки SSConnect с вложенными happ://add. Настройки, профили и идентификаторы устройств сохраняются при обновлении.')
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
