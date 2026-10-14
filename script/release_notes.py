import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- В общих настройках VK-релея выбирается только источник хешей: ручные значения задаются внутри профилей qWDTT.")
print("- В профилях qWDTT можно выбрать ручной или автоматический источник хешей. Отвязка VK возвращает профили в ручной режим.")
print("- Добавлен SSH-деплой qWDTT и CSQTT через официальные установщики, с консолью и экспортом конфигурации.")
print()
print(f"Основа: Throne {version['throne_version']}, ветка dev, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
