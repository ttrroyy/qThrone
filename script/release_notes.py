import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "version.json").read_text(encoding="utf-8"))
print('- Привязка VK: доступ для звонков запрашивается сразу в окне входа, на том же домене vk.ru. Получение хешей больше не открывает второе окно с повторным входом, которое заканчивалось ошибкой.')
print('- Повторное нажатие пинга перезапускает тест: идущая проверка останавливается и начинается заново, индикатор в строке крутится с начала. Для qWDTT остановка теперь прерывает и само TURN/DTLS-рукопожатие. Спидтест по-прежнему запускается только один.')
print()
print(f"Основа: Throne {version['throne_version']}, коммит [{version['throne_commit'][:8]}](https://github.com/throneproj/Throne/commit/{version['throne_commit']}).")
