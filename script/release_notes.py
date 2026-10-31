import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print(f"# qThrone {version['version']}")
print()
print("- Профили маршрутизации Happ и Incy (например, RoscomVPN) можно импортировать как отдельный профиль маршрутизации, как в мобильной версии: ссылку `happ://routing/...` или `incy://routing/...` можно вставить в окне профилей маршрутизации или прямо в главное окно. Такой профиль при подключении берёт DNS и файлы geoip.dat и geosite.dat из своего документа, а его категории `geosite:` и `geoip:` в правилах отмечаются предупреждением, а не ошибкой. Раньше такую маршрутизацию можно было добавить только к подписке.")
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
