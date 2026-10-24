import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print('- Пинг всей группы qWDTT, CSQTT и WDTT Plus больше не помечает часть профилей «Недоступен». Профили одной подписки используют одни и те же звонки VK, и одновременные проверки мешали друг другу: часть падала, хотя по одному все пинговались. Теперь профили с общими звонками VK проверяются по очереди, остальные по-прежнему параллельно.')
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
