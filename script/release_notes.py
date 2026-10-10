import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- Ограничено ожидание запуска проверок CSQTT, включая очередь сессий и окна капчи: 30 секунд для фоновых проверок, до трёх минут для ручных проверок с капчей.")
print("- Подтверждённый отказ авторизации или несовместимость протокола немедленно останавливают сессию CSQTT. В журнал выводится понятная причина без паролей, хешей и токенов.")
print("- Переведены на русский язык поля, подсказки и сообщения проверки ввода в редакторе CSQTT.")
print("- Белая иконка TUN сохраняет различимые оттенки граней и прозрачность краёв.")
print()
print(f"Основа: Throne {version['throne_version']}, ветка dev, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
