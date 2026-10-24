import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print('- В настройках TUN появился переключатель «Блокировать QUIC», по умолчанию включён. QUIC (UDP 443) теперь отклоняется только для трафика, который идёт через прокси: браузер сразу переходит на TCP и не зависает. Сайты и приложения, которые идут напрямую, снова могут пользоваться QUIC. Если выключить переключатель, QUIC не блокируется совсем.')
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
