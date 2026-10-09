# qThrone

Кроссплатформенный клиент для управления прокси на Qt с ядрами [sing-box](https://github.com/SagerNet/sing-box) и [Xray](https://github.com/XTLS/Xray-core).

Форк [Throne](https://github.com/throneproj/Throne) с поддержкой qWDTT в режимах RAW и WG. Сохранены интерфейс, маршрутизация и управление подключениями оригинального проекта.

[Скачать](https://github.com/tttroyy/qThrone/releases) · [Исходный Throne](https://github.com/throneproj/Throne)

## Возможности

- Системный прокси и TUN.
- Профили qWDTT (RAW) и qWDTT (WG); RAW выбран по умолчанию.
- Четыре отдельных поля для хешей звонков VK.
- Импорт ссылок qWDTT и подписок, экспорт настроек профиля.
- Выбор транспорта TURN UDP/TCP и маскировки Audio/Video.
- Белая иконка в трее при включённом TUN.
- Проверка обновлений из репозитория qThrone.
- Профили, группы, правила маршрутизации и подписки Throne.

## Платформы

| Система | Сборки |
|---|---|
| Windows | x64, ARM64 и x86; ZIP и установщик EXE; legacy-варианты |
| Linux | x64 и ARM64; ZIP, DEB и RPM; варианты с системным Qt |
| macOS | Intel и Apple Silicon; ZIP с приложением; legacy-вариант Intel |

Исполняемый файл внутри пакета сохраняет имя `Throne.exe` / `Throne`, приложение macOS — `Throne.app`.

## Протоколы

qWDTT RAW/WG, SOCKS, HTTP(S), Shadowsocks, Trojan, VMess, VLESS, Xray VLESS, TUIC, Hysteria/Hysteria2, AnyTLS, Mieru, Snell, NaïveProxy, Juicity, TrustTunnel, ShadowTLS, WireGuard/AmneziaWG, MASQUE, SSH, OpenVPN и OpenConnect.

Также доступны пользовательские конфигурации sing-box и Xray, цепочки подключений и дополнительные ядра.

## qWDTT

Для подключения нужны совместимый VPS, пароль и от одного до четырёх хешей звонков VK. RAW требует включённого RAW listener на сервере; для сервера с поддержкой только WireGuard выберите WG.

Основной порт профиля используется в режиме WG, отдельный RAW-порт — в режиме RAW. Локальные порты клиент выделяет автоматически. Устанавливать отдельное приложение WireGuard не требуется.

Поддерживаются ссылки `qwdtt://config`, `qwdtt:config`, старые `wdtt://`, текстовые и Base64-подписки, JSON-профили. Если режим не указан, выбирается RAW.

Для qWDTT сохраняются ограничения дополнительных ядер Throne: профиль не участвует в Auto Selector, IP-сканере и массовых serverless-тестах. IP-туннель использует IPv4. Решение капчи VK выполняется встроенным Go/RJS-кодом; Android WebView fallback отсутствует.

## Подписки

Поддерживаются ссылки профилей, JSON-конфигурации sing-box и Xray, формат ссылок v2rayN, а также поддерживаемые Throne форматы Shadowsocks и Clash.

## Примечания

- TUN требует системных прав доступа, как в оригинальном Throne.
- Завершайте приложение штатно, чтобы оно восстановило настройки системного прокси.
- Сборки `system-qt` для Linux используют Qt из системы; остальные пакеты содержат необходимые библиотеки Qt.
- Сборки macOS не подписаны сертификатом Apple. Особенности запуска описаны в [документации Throne](https://github.com/throneproj/Throne#note-on-macos-releases).

## Используемые проекты

- [Throne](https://github.com/throneproj/Throne)
- [qWDTT](https://github.com/SpaceNeuroX/proxy-turn-vk-android)
- [PWDTT](https://github.com/luminescq/PWDTT) — desktop-клиент, использованный для изучения поведения протокола
- [sing-box](https://github.com/SagerNet/sing-box)
- [Xray-core](https://github.com/XTLS/Xray-core)
- [Qv2ray](https://github.com/Qv2ray/Qv2ray)
- [Qt](https://www.qt.io/)
- [simple-protobuf](https://github.com/tonda-kriz/simple-protobuf)
- [fkYAML](https://github.com/fktn-k/fkYAML)
- [quirc](https://github.com/dlbeer/quirc)
- [SQLiteCpp](https://github.com/srombauts/SQLiteCpp)

## Лицензия

[GPL-3.0](LICENSE). Лицензия исходников qWDTT сохранена в [qwdtt/LICENSE](qwdtt/LICENSE).
