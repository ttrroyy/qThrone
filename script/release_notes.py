import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print('- Обфускация WireGuard. В редакторе WireGuard появилась кнопка «Обфускация»: wg-obfuscator для серверов ClusterM и Phobos с маскировкой под звонок (STUN) или под видеопоток видеозвонка (MEDIA), и wgobfs для серверов xt_wgobfs и rs-wgobfs. Обфускация работает прямо в ядре, без отдельных программ, поэтому пинг, проверка скорости, TUN, цепочки и раздельная маршрутизация работают так же, как с обычным WireGuard.')
print('- Ссылки phobos:// добавляются из буфера обмена, по QR-коду, из файла и из подписок. Это может быть WireGuard с обфускатором или режим SOCKS5 сервера Phobos с маскировкой STUN, MEDIA или TLS (поток выглядит как обычная сессия HTTPS). Для SOCKS5 ключ Phobos и маскировку можно указать и вручную в профиле SOCKS. Режим SOCKS5 передаёт только TCP.')
print('- Новые DNS-туннели CottenDNS и Soar. Оба работают через свои оригинальные клиенты, как MasterDNS, с пингом, проверкой скорости и раздельной маршрутизацией. Soar сам ищет рабочие резолверы страны, если это не выключено в профиле.')
print('- Добавляются ссылки cottendns://, soar:// и stormdns://, а также файлы профилей этих приложений. Профили StormDNS запускаются клиентом MasterDNS, с которым он совместим. CottenDNS и Soar можно создать и вручную: они есть в списке типов профиля.')
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
