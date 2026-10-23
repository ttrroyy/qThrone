import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print('- CSQTT: Steam и другие игры с UDP снова находят свои релеи. Все UDP-запросы одного соединения идут через один порт туннеля и принимают ответы с любого адреса, как в Android-версии. Буферы между клиентом и транспортом CSQTT увеличены, поэтому всплески пакетов больше не теряются.')
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
