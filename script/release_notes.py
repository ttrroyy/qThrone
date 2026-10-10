import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print("Первый стабильный выпуск qThrone с поддержкой qWDTT и оригинального CSQTT amurcanov.")
print()
print("- Пинг-тесты сохраняют выбранный режим капчи. Автоматические попытки выполняются без видимого окна; окно VK открывается для ручного шага.")
print("- CSQTT получает ограниченное время на первый запрос после запуска; измеряемый запрос использует таймаут из настроек. Для уже подключённого qWDTT/CSQTT убран лишний повторный замер.")
print("- Отдельный пинг-тест CSQTT использует IPv4, как его оригинальный IP-туннель.")
print("- Завершение CSQTT ожидает остановки дочернего транспорта; на Windows дочерний процесс также завершается при принудительном закрытии моста.")
print("- Сохранены исправления TUN, DNS внутри CSQTT, русский редактор, импорт имени профиля из name/remark/фрагмента ссылки и три оттенка значков.")
print()
print(f"Основа: Throne {version['throne_version']}, ветка dev, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
