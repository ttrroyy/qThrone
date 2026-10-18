# validate_deploy_data_mode and secure_preserved_config, verbatim from WDTT Plus deploy.sh 3.13
# (github.com/Ivan4537/WDTT-Plus f706ccd, the deploy.sh of the pinned v20 APK), GPL-3.0.
# The test defines the paths and settings deploy.sh declares at its top.
die() { echo "error: $*"; exit 1; }

secure_preserved_config() {
    [ -d "$WDTT_CONFIG_DIR" ] || return 0
    [ ! -L "$WDTT_CONFIG_DIR" ] || die "$WDTT_CONFIG_DIR не должен быть символической ссылкой"
    local name path
    for name in "$WDTT_ACCESS_DB" "$WDTT_ACCESS_DB_PREVIOUS" "$WDTT_WG_KEYS" "outbound-profile.env"; do
        path="$WDTT_CONFIG_DIR/$name"
        [ -e "$path" ] || continue
        [ -f "$path" ] && [ ! -L "$path" ] || die "Небезопасный объект конфигурации: $path"
        chmod 600 "$path" || die "Не удалось защитить $path"
    done
}

validate_deploy_data_mode() {
    local database="$WDTT_CONFIG_DIR/$WDTT_ACCESS_DB"
    case "$WDTT_INSTALL_MODE" in
        fresh)
            [ "$WDTT_PRESERVE_DATA" = "0" ] ||
                die "Первичная установка несовместима с режимом сохранения"
            if [ -e "$WDTT_SYSTEMD_UNIT_PATH" ] || [ -e "$WDTT_SERVER_BINARY_PATH" ] ||
               [ -e "$WDTT_CONFIG_DIR" ]; then
                die "Первичная установка остановлена: после проверки появились следы WDTT"
            fi
            ;;
        preserve)
            [ "$WDTT_PRESERVE_DATA" = "1" ] ||
                die "Обновление с сохранением запущено без защиты данных"
            if [ -f "$WDTT_STANDALONE_OWNERSHIP_PATH" ] &&
               grep -Fqx "${WDTT_STANDALONE_MARKER}" "$WDTT_STANDALONE_OWNERSHIP_PATH"; then
                die "Обновление остановлено: сервер управляется standalone-инсталлером"
            fi
            [ -d "$WDTT_CONFIG_DIR" ] && [ ! -L "$WDTT_CONFIG_DIR" ] ||
                die "Обновление остановлено: каталог данных Android-установки изменился после проверки"
            if [ -f "$WDTT_CONFIG_DIR/.android-deploy-preserved" ] &&
               [ ! -L "$WDTT_CONFIG_DIR/.android-deploy-preserved" ] &&
               grep -Fqx "$WDTT_ANDROID_PRESERVED_MARKER" "$WDTT_CONFIG_DIR/.android-deploy-preserved"; then
                [ ! -e "$WDTT_SYSTEMD_UNIT_PATH" ] && [ ! -e "$WDTT_SERVER_BINARY_PATH" ] &&
                  [ -f "$WDTT_CONFIG_DIR/$WDTT_ACCESS_DB" ] && [ ! -L "$WDTT_CONFIG_DIR/$WDTT_ACCESS_DB" ] &&
                  [ -f "$WDTT_CONFIG_DIR/$WDTT_WG_KEYS" ] && [ ! -L "$WDTT_CONFIG_DIR/$WDTT_WG_KEYS" ] ||
                    die "Обновление остановлено: сохранённые данные Android-деплоя изменились после проверки"
            else
              [ -f "$WDTT_SYSTEMD_UNIT_PATH" ] && [ ! -L "$WDTT_SYSTEMD_UNIT_PATH" ] &&
                [ -x "$WDTT_SERVER_BINARY_PATH" ] && [ ! -L "$WDTT_SERVER_BINARY_PATH" ] ||
                  die "Обновление остановлено: признаки Android-установки изменились после проверки"
              if ! grep -Fqx "# ${WDTT_ANDROID_DEPLOY_MARKER}" "$WDTT_SYSTEMD_UNIT_PATH"; then
                [ -f "$WDTT_CONFIG_DIR/$WDTT_WG_KEYS" ] &&
                  [ ! -L "$WDTT_CONFIG_DIR/$WDTT_WG_KEYS" ] &&
                  grep -Eq '^ExecStart=/usr/local/bin/wdtt-server([[:space:]]|$)' "$WDTT_SYSTEMD_UNIT_PATH" &&
                  grep -Eq '(^|[[:space:]])-config-dir[[:space:]]+/etc/wdtt([[:space:]]|$)' "$WDTT_SYSTEMD_UNIT_PATH" &&
                  grep -Fq 'wdtt0' "$WDTT_SYSTEMD_UNIT_PATH" ||
                    die "Обновление остановлено: не удалось подтвердить старую Android-установку"
              fi
            fi
            ;;
        reset)
            [ "$WDTT_PRESERVE_DATA" = "0" ] ||
                die "Полный сброс несовместим с режимом сохранения"
            ;;
        *)
            die "Приложение не указало безопасный режим установки"
            ;;
    esac
    if [ -e "$database" ] && [ "$WDTT_PRESERVE_DATA" != "1" ]; then
        die "Найдена существующая база WDTT. Обновление разрешено только в режиме сохранения; явный сброс должен заранее удалить старую базу после подтверждения в приложении."
    fi
    if [ "$WDTT_PRESERVE_DATA" = "1" ]; then
        [ -f "$database" ] && [ ! -L "$database" ] && [ -s "$database" ] ||
            die "Режим сохранения требует обычный непустой passwords.json"
        secure_preserved_config
    fi
    if [ -n "$WDTT_STAGED_DB" ]; then
        [ -f "$WDTT_STAGED_DB" ] && [ ! -L "$WDTT_STAGED_DB" ] && [ -s "$WDTT_STAGED_DB" ] ||
            die "Подготовленная база WDTT отсутствует или небезопасна"
        [ "$(stat -c '%a' "$WDTT_STAGED_DB")" = "600" ] ||
            die "Подготовленная база WDTT должна иметь права 600"
    fi
}
