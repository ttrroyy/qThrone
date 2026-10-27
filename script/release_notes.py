import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print('- Ссылки openflux://import теперь добавляются. Это профили форка OpenFlux с панелью ключей (wlruscfd/openflux-server). Его серверы работают по своему протоколу, поэтому такие профили запускает собственный клиент форка. Тип называется OpenFlux Key, его можно создать и вручную. Пинг, проверка скорости и маршрутизация работают как у остальных профилей.')
print('- Проверка «я не робот» от Яндекса для OpenFlux и OpenFlux Key открывается в отдельном окне Edge или Chrome со своим временным профилем, как капча VK. Ваш браузер, вкладки и cookies не затрагиваются. У OpenFlux результат уходит, когда страница проверки сменилась обычной страницей Яндекса. У OpenFlux Key, как в приложении форка, когда загрузился сам документ. Если закрыть окно, подключение не обрывается: клиент спросит снова чуть позже. При фоновых проверках пинга окно не открывается.')
print('- Конфиги WARP с AmneziaWG, где IPv4 и IPv6 записаны отдельными строками Address, снова работают. Раньше оставался только последний адрес, и подключение не поднималось.')
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
