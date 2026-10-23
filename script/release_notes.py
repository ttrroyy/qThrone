import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print('- Сервисы Google больше не зависают в браузере: QUIC (UDP 443) через туннель отклоняется, и браузер сразу переходит на обычное HTTPS-соединение.')
print('- Если IPv6 в TUN выключен, DNS не отдаёт IPv6-адреса: браузер не уходит мимо туннеля и не ждёт недоступный IPv6.')
print('- Оптимистичный кэш DNS включён по умолчанию, в том числе у существующих установок. При включённом Fake-IP таблица адресов сохраняется между перезапусками.')
print('- Пинг-тест: первому запросу к серверу даётся на 5 секунд больше, поэтому медленные, но рабочие серверы больше не помечаются недоступными. qWDTT и другие нативные транспорты не задерживают следующую сотню профилей. Пачка профилей проверяется перед тестом одним запросом.')
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
