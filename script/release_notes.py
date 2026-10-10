import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("- DNS для имён назначения CSQTT передаётся через TCP внутри туннеля. Потеря первого UDP-запроса DNS больше не вызывает этот таймаут проверки задержки; пользовательский UDP-трафик остаётся UDP.")
print("- Мост CSQTT начинает обслуживать подключения после получения конфигурации и события готовности рабочего канала оригинального транспорта.")
print("- Имя профиля CSQTT импортируется из name, remark или фрагмента ссылки и сохраняется при экспорте. Если панель не включает примечание в ссылку, клиент не может его восстановить.")
print("- Фиолетовая иконка системного прокси отображает три более заметных оттенка граней.")
print()
print(f"Основа: Throne {version['throne_version']}, ветка dev, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
