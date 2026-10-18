#include "include/ui/setting/dialog_wdttplus_exit.h"
#include "include/ui/setting/dialog_vk_relay.h"
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
class WdttPlusExit : public QDialog {
    QJsonObject server;
    RelayTask *task;
    QJsonObject snapshot;
    bool busy = false;
    QList<QWidget *> actions;
    QLabel *mode, *conflict, *running, *step;
    QProgressBar *progress;
    QPlainTextEdit *result;
    // Local proxy, external proxy, TUN, second VPS, WARP and WireGuard file fields.
    QSpinBox *localPort, *externalPort, *vpsSshPort, *vpsWgPort, *warpMtu;
    QLineEdit *localLogin, *localPassword, *externalHost, *externalLogin, *externalPassword, *tunName, *vpsHost, *vpsUser, *vpsPassword;
    QComboBox *externalKind;
    QListWidget *tunFound;
    QPlainTextEdit *wgConfig;

    // WDTT Plus's runOutboundAction: the core checks route conflicts first and re-reads the server after.
    void run(const QString &label, QJsonObject request, const std::function<void(const QJsonObject &)> &extra = {}) {
        if (busy) return;
        busy = true;
        for (auto *widget : actions) widget->setEnabled(false);
        running->setText(label);
        step->clear();
        result->clear();
        progress->show();
        request["label"] = label;
        auto answered = std::make_shared<bool>(false);
        task->start({{"operation", "wdttplus-exit"}, {"server", server}, {"value", QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact))}},
                    [this, answered, extra, label](const QJsonObject &event) {
                        const auto type = event["type"].toString();
                        if (type == "progress") step->setText(event["text"].toString());
                        if (type == "trust")
                            task->approve(QMessageBox::question(this, QStringLiteral("SSH-ключ второго VPS"),
                                                                QStringLiteral("Первое подключение ко второму VPS.\n\n%1\n\nДоверять этому ключу?").arg(event["text"].toString())) ==
                                          QMessageBox::Yes);
                        if (type == "result") {
                            *answered = true;
                            const auto value = event["value"].toObject();
                            showResult(value["text"].toString(), value["failed"].toBool());
                            if (value.contains("snapshot")) apply(value["snapshot"].toObject());
                            if (extra) extra(value);
                        }
                        if (type == "error") {
                            *answered = true;
                            showResult(event["text"].toString(), true);
                        }
                        if (type == "finished") {
                            busy = false;
                            progress->hide();
                            for (auto *widget : actions) widget->setEnabled(true);
                            if (!*answered) showResult(label + QStringLiteral(": операция прервана"), true);
                        }
                    });
    }

    void showResult(const QString &text, bool failed) {
        result->setPlainText(text);
        result->setStyleSheet(failed ? "color: #e86e64" : QString());
    }

    void apply(const QJsonObject &value) {
        snapshot = value;
        mode->setText(value["modeLabel"].toString(QStringLiteral("не загружен")));
        conflict->setText(value["routeConflict"].toString());
        conflict->setVisible(!conflict->text().isEmpty());
        const auto port = [](QSpinBox *box, const QJsonValue &v, int fallback) { bool ok; const int n = v.toString().toInt(&ok); box->setValue(ok ? n : fallback); };
        port(localPort, value["localProxyPort"], 1080);
        localLogin->setText(value["localProxyLogin"].toString());
        localPassword->setText(value["localProxyPassword"].toString());
        externalKind->setCurrentIndex(qMax(0, externalKind->findData(value["externalProxyKindName"].toString())));
        externalHost->setText(value["externalProxyHost"].toString());
        port(externalPort, value["externalProxyPort"], 1080);
        externalLogin->setText(value["externalProxyLogin"].toString());
        externalPassword->setText(value["externalProxyPassword"].toString());
        tunName->setText(value["tunInterface"].toString());
        vpsHost->setText(value["wireGuardExitHost"].toString());
        port(vpsSshPort, value["wireGuardExitSshPort"], 22);
        vpsUser->setText(value["wireGuardExitUser"].toString().isEmpty() ? QStringLiteral("root") : value["wireGuardExitUser"].toString());
        port(vpsWgPort, value["wireGuardExitPort"], 51820);
        port(warpMtu, value["warpMtu"], 1280);
        wgConfig->setPlainText(value["importedWireGuardConfig"].toString());
    }

    bool confirm(const QString &question) {
        return QMessageBox::question(this, windowTitle(), question) == QMessageBox::Yes;
    }

    QPushButton *button(const QString &text, QBoxLayout *row, const std::function<void()> &clicked) {
        auto *b = new QPushButton(text, this);
        connect(b, &QPushButton::clicked, this, clicked);
        row->addWidget(b);
        actions.append(b);
        return b;
    }

    QWidget *page(QTabWidget *tabs, const QString &title, const QString &hint, QFormLayout *&form, QHBoxLayout *&buttons) {
        auto *widget = new QWidget(tabs);
        auto *layout = new QVBoxLayout(widget);
        auto *description = new QLabel(hint, widget);
        description->setWordWrap(true);
        layout->addWidget(description);
        form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        layout->addLayout(form);
        buttons = new QHBoxLayout;
        layout->addLayout(buttons);
        layout->addStretch();
        tabs->addTab(widget, title);
        return widget;
    }

    QSpinBox *spin(int minimum, int maximum, int value) {
        auto *box = new QSpinBox(this);
        box->setRange(minimum, maximum);
        box->setValue(value);
        return box;
    }

    QLineEdit *edit(bool secret = false) {
        auto *line = new QLineEdit(this);
        if (secret) line->setEchoMode(QLineEdit::Password);
        return line;
    }

public:
    WdttPlusExit(QWidget *parent, QJsonObject value) : QDialog(parent), server(std::move(value)) {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(QStringLiteral("Выходной IP и прокси — ") + (server["name"].toString().isEmpty() ? server["host"].toString() : server["name"].toString()));
        resize(900, 760);
        task = new RelayTask(this);
        auto *layout = new QVBoxLayout(this);

        auto *current = new QGroupBox(QStringLiteral("Текущий выход"), this);
        auto *currentLayout = new QVBoxLayout(current);
        mode = new QLabel(QStringLiteral("не загружен"), current);
        mode->setStyleSheet("font-weight: bold; font-size: 15px");
        conflict = new QLabel(current);
        conflict->setWordWrap(true);
        conflict->setStyleSheet("color: #e8aa50");
        conflict->hide();
        currentLayout->addWidget(mode);
        currentLayout->addWidget(conflict);
        auto *common = new QHBoxLayout;
        button(QStringLiteral("Загрузить настройки"), common, [this] { run(QStringLiteral("Читаю настройки выходного IP"), {{"action", "load"}}); });
        button(QStringLiteral("Статус"), common, [this] { run(QStringLiteral("Проверяю текущий выход WDTT"), {{"action", "status"}}); });
        button(QStringLiteral("Диагностика"), common, [this] { run(QStringLiteral("Собираю диагностику выхода WDTT"), {{"action", "diagnostics"}}); });
        common->addStretch();
        button(QStringLiteral("Вернуть прямой выход"), common, [this] { run(QStringLiteral("Возвращаю прямой выход WDTT"), {{"action", "direct"}}); });
        currentLayout->addLayout(common);
        layout->addWidget(current);

        auto *tabs = new QTabWidget(this);
        QFormLayout *form;
        QHBoxLayout *row;
        page(tabs, QStringLiteral("Прокси на этом VPS"), QStringLiteral("Устанавливает SOCKS5 (3proxy) на этом сервере. Клиенты WDTT выходят как обычно, а прокси можно использовать отдельно."), form, row);
        localPort = spin(1, 65533, 1080);
        localLogin = edit();
        localPassword = edit(true);
        form->addRow(QStringLiteral("Порт SOCKS5"), localPort);
        form->addRow(QStringLiteral("Логин"), localLogin);
        form->addRow(QStringLiteral("Пароль"), localPassword);
        const auto local = [this](const QString &action) {
            return QJsonObject{{"action", action}, {"port", localPort->value()}, {"login", localLogin->text()}, {"password", localPassword->text()}};
        };
        button(QStringLiteral("Установить"), row, [this, local] { run(QStringLiteral("Устанавливаю прокси на этом сервере"), local("local-install")); });
        button(QStringLiteral("Проверить"), row, [this, local] { run(QStringLiteral("Проверяю прокси на этом сервере"), local("local-check")); });
        button(QStringLiteral("Остановить"), row, [this] { run(QStringLiteral("Останавливаю прокси на этом сервере"), {{"action", "local-stop"}}); });
        button(QStringLiteral("Удалить"), row, [this] {
            if (confirm(QStringLiteral("Удалить прокси с этого сервера?"))) run(QStringLiteral("Удаляю прокси с этого сервера"), {{"action", "local-remove"}});
        });

        page(tabs, QStringLiteral("Внешний TCP-прокси"), QStringLiteral("TCP-трафик клиентов WDTT пойдёт через указанный прокси (redsocks), остальной трафик сервера не меняется."), form, row);
        externalKind = new QComboBox(this);
        externalKind->addItem("SOCKS5", "Socks5");
        externalKind->addItem("HTTP", "Http");
        externalHost = edit();
        externalPort = spin(1, 65535, 1080);
        externalLogin = edit();
        externalPassword = edit(true);
        form->addRow(QStringLiteral("Тип"), externalKind);
        form->addRow(QStringLiteral("Адрес прокси"), externalHost);
        form->addRow(QStringLiteral("Порт"), externalPort);
        form->addRow(QStringLiteral("Логин (необязательно)"), externalLogin);
        form->addRow(QStringLiteral("Пароль"), externalPassword);
        const auto external = [this](const QString &action) {
            return QJsonObject{{"action", action}, {"kind", externalKind->currentData().toString()}, {"host", externalHost->text().trimmed()},
                               {"port", externalPort->value()}, {"login", externalLogin->text()}, {"password", externalPassword->text()}};
        };
        button(QStringLiteral("Включить"), row, [this, external] { run(QStringLiteral("Проверяю и включаю внешний TCP-прокси"), external("external-enable")); });
        button(QStringLiteral("Проверить"), row, [this, external] { run(QStringLiteral("Проверяю доступность внешнего TCP-прокси"), external("external-check")); });
        button(QStringLiteral("Удалить"), row, [this] {
            if (confirm(QStringLiteral("Удалить настройки внешнего TCP-прокси?"))) run(QStringLiteral("Удаляю настройки внешнего TCP-прокси"), {{"action", "external-delete"}});
        });

        auto *tunPage = page(tabs, QStringLiteral("Существующий TUN-интерфейс"),
                             QStringLiteral("Клиенты WDTT выйдут через интерфейс VPN, который уже работает на сервере. Если он упадёт, трафик клиентов блокируется, а не уходит напрямую."), form, row);
        tunName = edit();
        form->addRow(QStringLiteral("Имя интерфейса"), tunName);
        tunFound = new QListWidget(tunPage);
        tunFound->setMaximumHeight(120);
        form->addRow(QStringLiteral("Найденные"), tunFound);
        connect(tunFound, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { tunName->setText(item->data(Qt::UserRole).toString()); });
        button(QStringLiteral("Найти интерфейсы"), row, [this] {
            run(QStringLiteral("Ищу интерфейсы на сервере"), {{"action", "tun-candidates"}}, [this](const QJsonObject &value) {
                tunFound->clear();
                for (const auto &entry : value["candidates"].toArray()) {
                    const auto o = entry.toObject();
                    auto *item = new QListWidgetItem(o["name"].toString() + (o["up"].toBool() ? QStringLiteral(" · работает") : QStringLiteral(" · остановлен")), tunFound);
                    item->setData(Qt::UserRole, o["name"].toString());
                }
                showResult(tunFound->count() ? QStringLiteral("Выберите интерфейс в списке.") : QStringLiteral("Подходящих интерфейсов не найдено."), false);
            });
        });
        button(QStringLiteral("Включить"), row, [this] { run(QStringLiteral("Включаю выход через TUN-интерфейс"), {{"action", "tun-enable"}, {"name", tunName->text().trimmed()}}); });
        button(QStringLiteral("Проверить"), row, [this] { run(QStringLiteral("Проверяю выход через TUN-интерфейс"), {{"action", "tun-check"}}); });
        button(QStringLiteral("Удалить"), row, [this] {
            if (confirm(QStringLiteral("Удалить настройку TUN-выхода?"))) run(QStringLiteral("Удаляю настройку TUN-выхода"), {{"action", "tun-delete"}});
        });

        page(tabs, QStringLiteral("Другой сервер"),
             QStringLiteral("На втором VPS ставится WireGuard, и клиенты WDTT выходят в интернет с его IP. Пароль второго сервера нужен только на время настройки и нигде не сохраняется."), form, row);
        vpsHost = edit();
        vpsSshPort = spin(1, 65535, 22);
        vpsUser = edit();
        vpsUser->setText("root");
        vpsPassword = edit(true);
        vpsWgPort = spin(1, 65535, 51820);
        form->addRow(QStringLiteral("Адрес второго VPS"), vpsHost);
        form->addRow(QStringLiteral("SSH-порт"), vpsSshPort);
        form->addRow(QStringLiteral("SSH-логин"), vpsUser);
        form->addRow(QStringLiteral("SSH-пароль"), vpsPassword);
        form->addRow(QStringLiteral("Порт WireGuard"), vpsWgPort);
        const auto foreign = [this] {
            return QJsonObject{{"host", vpsHost->text().trimmed()}, {"sshPort", vpsSshPort->value()}, {"user", vpsUser->text().trimmed()}, {"password", vpsPassword->text()}};
        };
        button(QStringLiteral("Настроить"), row, [this, foreign] {
            run(QStringLiteral("Настраиваю WireGuard-выход через другой сервер"), {{"action", "vps-install"}, {"foreign", foreign()}, {"wgPort", vpsWgPort->value()}});
            vpsPassword->clear();
        });
        button(QStringLiteral("Проверить"), row, [this] { run(QStringLiteral("Проверяю выход через другой сервер"), {{"action", "vps-check"}}); });
        button(QStringLiteral("Удалить"), row, [this, foreign] {
            if (!confirm(QStringLiteral("Удалить выход через другой сервер?"))) return;
            run(QStringLiteral("Удаляю выход через другой сервер"), {{"action", "vps-delete"}, {"foreign", foreign()}});
            vpsPassword->clear();
        });

        page(tabs, QStringLiteral("Бесплатный WARP"), QStringLiteral("Клиенты WDTT выходят через Cloudflare WARP (wgcf). Сервер сам следит за соединением и перезапускает его при сбое."), form, row);
        warpMtu = spin(1280, 1500, 1280);
        form->addRow(QStringLiteral("MTU"), warpMtu);
        button(QStringLiteral("Установить / восстановить"), row, [this] { run(QStringLiteral("Устанавливаю и проверяю бесплатный WARP"), {{"action", "warp-install"}, {"mtu", warpMtu->value()}}); });
        button(QStringLiteral("Проверить"), row, [this] { run(QStringLiteral("Проверяю бесплатный WARP"), {{"action", "warp-check"}}); });
        button(QStringLiteral("Перезапустить"), row, [this] { run(QStringLiteral("Перезапускаю и проверяю бесплатный WARP"), {{"action", "warp-restart"}}); });
        button(QStringLiteral("Обновить wgcf"), row, [this] { run(QStringLiteral("Проверяю обновление wgcf"), {{"action", "warp-update"}}); });
        button(QStringLiteral("Сбросить регистрацию"), row, [this] {
            if (confirm(QStringLiteral("Сбросить регистрацию WARP?"))) run(QStringLiteral("Сбрасываю регистрацию WARP"), {{"action", "warp-reset"}});
        });
        button(QStringLiteral("Удалить"), row, [this] {
            if (confirm(QStringLiteral("Удалить бесплатный WARP?"))) run(QStringLiteral("Удаляю бесплатный WARP и возвращаю прямой выход"), {{"action", "warp-delete"}});
        });

        page(tabs, QStringLiteral("VPN/WireGuard-файл"), QStringLiteral("Конфиг WireGuard применяется только к клиентам WDTT. Опасные команды и DNS из файла убираются перед загрузкой."), form, row);
        wgConfig = new QPlainTextEdit(this);
        wgConfig->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        wgConfig->setMinimumHeight(160);
        form->addRow(QStringLiteral("Конфиг"), wgConfig);
        button(QStringLiteral("Выбрать файл…"), row, [this] {
            const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Конфиг WireGuard"), {}, "WireGuard (*.conf);;Все файлы (*)");
            QFile file(path);
            if (!path.isEmpty() && file.open(QIODevice::ReadOnly)) wgConfig->setPlainText(QString::fromUtf8(file.read(64 * 1024)));
        });
        button(QStringLiteral("Проверить файл"), row, [this] { run(QStringLiteral("Проверяю VPN/WireGuard-файл"), {{"action", "wg-validate"}, {"config", wgConfig->toPlainText()}}); });
        button(QStringLiteral("Включить"), row, [this] { run(QStringLiteral("Включаю VPN/WireGuard-выход из файла"), {{"action", "wg-enable"}, {"config", wgConfig->toPlainText()}}); });
        button(QStringLiteral("Проверить сервер"), row, [this] { run(QStringLiteral("Проверяю установленный VPN/WireGuard-выход"), {{"action", "wg-check"}}); });
        button(QStringLiteral("Удалить"), row, [this] {
            if (confirm(QStringLiteral("Удалить VPN/WireGuard-файл с сервера?"))) run(QStringLiteral("Удаляю VPN/WireGuard-файл"), {{"action", "wg-delete"}});
        });
        layout->addWidget(tabs);

        auto *output = new QGroupBox(QStringLiteral("Результат"), this);
        auto *outputLayout = new QVBoxLayout(output);
        running = new QLabel(output);
        running->setStyleSheet("font-weight: bold");
        step = new QLabel(output);
        step->setWordWrap(true);
        progress = new QProgressBar(output);
        progress->setRange(0, 0);
        progress->setMaximumHeight(6);
        progress->setTextVisible(false);
        progress->hide();
        result = new QPlainTextEdit(output);
        result->setReadOnly(true);
        result->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        result->setMinimumHeight(160);
        outputLayout->addWidget(running);
        outputLayout->addWidget(progress);
        outputLayout->addWidget(step);
        outputLayout->addWidget(result);
        layout->addWidget(output, 1);
        run(QStringLiteral("Читаю настройки выходного IP"), {{"action", "load"}});
    }
};
}

void OpenWdttPlusExit(QWidget *parent, const QJsonObject &server) {
    (new WdttPlusExit(parent, server))->show();
}
