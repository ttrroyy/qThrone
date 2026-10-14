#include "include/ui/setting/dialog_vk_relay.h"
#include "include/configs/outbounds/qwdtt.h"
#include "include/database/ProfilesRepo.h"
#include "include/global/Configs.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/profile/edit_qwdtt.h"
#include "include/ui/setting/ThemeManager.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

RelayTask::RelayTask(QObject *parent) : QObject(parent) {
    connect(&process, &QProcess::started, this, [this] { process.write(requestBytes); });
    connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
        pending += process.readAllStandardOutput();
        while (pending.contains('\n')) {
            const int end = pending.indexOf('\n');
            const auto line = pending.left(end);
            pending.remove(0, end + 1);
            const auto event = QJsonDocument::fromJson(line).object();
            if (event["type"] == "error")
                failed = true;
            if (!event.isEmpty() && handler)
                handler(event);
        }
    });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        failed = true;
        if (handler)
            handler({{"type", "error"}, {"text", QStringLiteral("Не удалось запустить помощник VK-релея")}});
        if (error == QProcess::FailedToStart) {
            queue.clear();
            if (handler)
                handler({{"type", "finished"}});
        }
    });
    connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        finishing = true;
        if (!failed && (code != 0 || status != QProcess::NormalExit) && handler)
            handler({{"type", "error"}, {"text", QStringLiteral("Операция прервана")}});
        if (handler)
            handler({{"type", "finished"}});
        finishing = false;
        if (!queue.isEmpty()) {
            const auto next = queue.dequeue();
            QTimer::singleShot(0, this, [this, next] { start(next.first, next.second); });
        }
    });
}
void RelayTask::start(const QJsonObject &request, Handler callback) {
    if (running() || finishing) {
        queue.enqueue({request, std::move(callback)});
        return;
    }
    handler = std::move(callback);
    ++generation;
    pending.clear();
    failed = false;
    process.setProgram(Configs::FindCoreRealPath());
    process.setArguments({"--relay"});
    requestBytes = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    process.start();
}
void RelayTask::approve(bool accepted) { process.write(accepted ? "approve\n" : "reject\n"); }
void RelayTask::cancel() {
    queue.clear();
    if (running()) {
        process.write("cancel\n");
        const auto cancelledGeneration = generation;
        QTimer::singleShot(6000, &process, [this, cancelledGeneration] {
            if (generation == cancelledGeneration && running())
                process.kill();
        });
    }
}
RelayTask::~RelayTask() {
    process.disconnect(this);
    if (running()) {
        process.write("cancel\n");
        if (!process.waitForFinished(1500)) {
            process.kill();
            process.waitForFinished(1000);
        }
    }
}
bool RelayTask::running() const { return process.state() != QProcess::NotRunning; }
QStringList RelayAccountHashes(int count, QString *error) {
    QProcess process;
    process.start(Configs::FindCoreRealPath(), {"--relay"});
    if (!process.waitForStarted(5000)) {
        *error = QStringLiteral("Не удалось запустить помощник VK-релея");
        return {};
    }
    process.write(
        QJsonDocument(QJsonObject{{"operation", "vk-hashes"}, {"count", count}}).toJson(QJsonDocument::Compact) + '\n');
    process.closeWriteChannel();
    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(5000);
        *error = QStringLiteral("VK: превышено время ожидания");
        return {};
    }
    QStringList result;
    for (const auto &line : process.readAllStandardOutput().split('\n')) {
        const auto event = QJsonDocument::fromJson(line).object();
        if (event["type"] == "result")
            for (const auto &value : event["value"].toArray())
                result.append(value.toString());
        if (event["type"] == "error")
            *error = event["text"].toString();
    }
    if (result.isEmpty() && error->isEmpty())
        *error = QStringLiteral("VK не вернул хеши");
    return result;
}
bool ResetQWDTTHashSources() {
    auto *settings = Configs::dataManager->settingsRepo.get();
    settings->vk_relay_linked = false;
    settings->qwdtt_hash_source = "manual";
    bool saved = settings->Save();
    for (int id : Configs::dataManager->profilesRepo->GetProfileIdsByType("qwdtt")) {
        const auto profile = Configs::dataManager->profilesRepo->GetProfile(id);
        if (profile)
            if (auto *outbound = dynamic_cast<Configs::qwdtt *>(profile->outbound.get());
                outbound && outbound->hashSource == "auto") {
                outbound->hashSource = "manual";
                saved = Configs::dataManager->profilesRepo->Save(profile) && saved;
            }
    }
    return saved;
}

namespace {
void autoEnabled(QComboBox *combo, bool enabled) {
    if (auto *model = qobject_cast<QStandardItemModel *>(combo->model()))
        model->item(1)->setEnabled(enabled);
}
QJsonObject freshServer(const QString &kind) {
    return {{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"kind", kind},
            {"name", "Новый сервер"},
            {"user", "root"},
            {"auth", "password"},
            {"sshPort", "22"},
            {"peerPort", kind == "qwdtt" ? "56000" : "46000"},
            {"wgPort", "56001"},
            {"directPort", "56002"},
            {"rawPort", "56003"},
            {"adminPort", "56002"},
            {"webPort", "46002"},
            {"dns1", "1.1.1.1"},
            {"dns2", "1.0.0.1"},
            {"deviceId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"adminToken", QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-') +
                               QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-')}};
}
class ServerEditor : public QDialog {
    QJsonObject server;
    QMap<QString, QLineEdit *> fields;
    QComboBox *auth;
    QCheckBox *manual;
    QCheckBox *docker = nullptr;
    QPlainTextEdit *console;
    QPlainTextEdit *configuration;
    QProgressBar *progress;
    QLabel *step;
    RelayTask *task;
    QList<QWidget *> controls;
    std::function<void(QJsonObject)> save;
    void capture() {
        for (auto it = fields.begin(); it != fields.end(); ++it)
            if (it.key() != "key")
                server[it.key()] = it.value()->text();
        server["auth"] = auth->currentData().toString();
        server["manualPorts"] = manual->isChecked() ? "true" : "false";
        if (docker)
            server["docker"] = docker->isChecked() ? "true" : "false";
    }
    void secrets() {
        QDialog dialog(this);
        dialog.setWindowTitle(server["kind"] == "qwdtt" ? "Секреты (бот, пароли)" : "Авторизация");
        auto *layout = new QVBoxLayout(&dialog);
        auto *form = new QFormLayout;
        layout->addLayout(form);
        QMap<QString, QLineEdit *> edits;
        QList<QPair<QString, QString>> keys{{"password", "Пароль туннеля"}};
        if (server["kind"] == "qwdtt")
            keys += {{"botToken", "Токен Telegram-бота"},
                     {"adminId", "Telegram ID администратора"},
                     {"adminToken", "Токен панели"}};
        else
            keys += {{"webUser", "Логин веб-панели"}, {"webPassword", "Пароль веб-панели"}, {"deviceId", "Device ID"}};
        for (const auto &[key, label] : keys) {
            auto *edit = new QLineEdit(server[key].toString(), &dialog);
            if (key.contains("Token") || key.contains("assword"))
                edit->setEchoMode(QLineEdit::Password);
            edits[key] = edit;
            form->addRow(label, edit);
        }
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            for (auto it = edits.begin(); it != edits.end(); ++it)
                server[it.key()] = it.value()->text();
            capture();
            save(server);
        }
    }
    void run(bool uninstall) {
        if (task->running())
            return;
        capture();
        if (uninstall &&
            QMessageBox::question(this, "Удалить сервис с VPS",
                                  "Удалить установленный сервис и его серверную конфигурацию с этого VPS?") !=
                QMessageBox::Yes)
            return;
        if (!manual->isChecked()) {
            const auto defaults = freshServer(server["kind"].toString());
            for (const auto &key : {"peerPort", "wgPort", "rawPort", "directPort", "adminPort", "webPort"})
                server[key] = defaults[key];
        }
        save(server);
        console->clear();
        progress->setValue(0);
        for (auto *widget : controls)
            widget->setEnabled(false);
        task->start(
            {{"operation", uninstall ? "uninstall" : "deploy"}, {"server", server}}, [this](const QJsonObject &event) {
                const auto type = event["type"].toString();
                const auto text = event["text"].toString();
                if (type == "trust")
                    task->approve(QMessageBox::question(this, "SSH-ключ VPS",
                                                        "Первое подключение к " + server["host"].toString() + ".\n\n" +
                                                            text + "\n\nДоверять этому ключу?") == QMessageBox::Yes);
                if (type == "host-key") {
                    server["hostKey"] = text;
                    save(server);
                }
                if (type == "admin-pin") {
                    server["adminPin"] = text;
                    save(server);
                }
                if (type == "log") {
                    console->moveCursor(QTextCursor::End);
                    console->insertPlainText(text);
                }
                if (type == "progress") {
                    step->setText(text);
                    if (event["value"].isDouble())
                        progress->setValue(event["value"].toInt());
                }
                if (type == "config") {
                    server["config"] = event["value"];
                    configuration->setPlainText(event["value"].toString());
                    save(server);
                }
                if (type == "error") {
                    step->setText(text);
                    console->appendPlainText("Ошибка: " + text);
                }
                if (type == "finished") {
                    for (auto *widget : controls)
                        widget->setEnabled(true);
                }
            });
    }

  protected:
    void closeEvent(QCloseEvent *event) override {
        if (task->running()) {
            if (QMessageBox::question(this, "Установка выполняется",
                                      "Остановить SSH-операцию? Уже выполненные изменения на VPS сохранятся.") !=
                QMessageBox::Yes) {
                event->ignore();
                return;
            }
            task->cancel();
        }
        capture();
        save(server);
        QDialog::closeEvent(event);
    }

  public:
    ServerEditor(QJsonObject value, std::function<void(QJsonObject)> callback, QWidget *parent)
        : QDialog(parent), server(std::move(value)), save(std::move(callback)) {
        setWindowTitle("Управление сервером — " + server["kind"].toString());
        resize(760, 760);
        task = new RelayTask(this);
        auto *outer = new QVBoxLayout(this);
        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        outer->addWidget(scroll);
        auto *body = new QWidget(scroll);
        scroll->setWidget(body);
        auto *layout = new QVBoxLayout(body);
        auto *secretsButton =
            new QPushButton(server["kind"] == "qwdtt" ? "Секреты (бот, пароли)" : "Авторизация", body);
        layout->addWidget(secretsButton);
        controls.append(secretsButton);
        connect(secretsButton, &QPushButton::clicked, this, [this] { secrets(); });
        auto *actions = new QHBoxLayout;
        layout->addLayout(actions);
        auto *install = new QPushButton("Установить", body);
        auto *remove = new QPushButton("Удалить сервис с VPS", body);
        actions->addWidget(install);
        actions->addWidget(remove);
        controls += {install, remove};
        connect(install, &QPushButton::clicked, this, [this] { run(false); });
        connect(remove, &QPushButton::clicked, this, [this] { run(true); });
        auto *group = new QGroupBox("Подключение к VPS", body);
        layout->addWidget(group);
        controls.append(group);
        auto *form = new QFormLayout(group);
        const auto add = [&](const QString &key, const QString &label, bool secret = false) {
            auto *edit = new QLineEdit(server[key].toString(), group);
            if (secret)
                edit->setEchoMode(QLineEdit::Password);
            fields[key] = edit;
            form->addRow(label, edit);
            return edit;
        };
        add("name", "Название сервера");
        add("host", "IP / домен");
        add("user", "Логин SSH");
        add("sshPort", "Порт SSH");
        auth = new QComboBox(group);
        auth->addItem("Пароль", "password");
        auth->addItem("SSH-ключ", "key");
        auth->setCurrentIndex(server["auth"] == "key" ? 1 : 0);
        form->addRow("Вход на сервер", auth);
        auto *password = add("sshPassword", "Пароль SSH", true);
        add("sudoPassword", "Пароль sudo (если отличается)", true);
        auto *key = add("key", "SSH-ключ");
        key->setReadOnly(true);
        key->setText(server["key"].toString().isEmpty() ? "Не загружен" : "SSH-ключ загружен");
        auto *loadKey = new QPushButton("Загрузить SSH-ключ…", group);
        form->addRow("", loadKey);
        auto *keyPassword = add("keyPassword", "Пароль SSH-ключа", true);
        connect(loadKey, &QPushButton::clicked, this, [this, key] {
            const auto path = QFileDialog::getOpenFileName(this, "SSH-ключ");
            if (!path.isEmpty()) {
                const auto bytes = ReadFileText(path);
                if (bytes.isEmpty()) {
                    QMessageBox::warning(this, "SSH-ключ", "Не удалось прочитать файл");
                    return;
                }
                server["key"] = bytes;
                key->setText(QFileInfo(path).fileName());
            }
        });
        const auto authVisibility = [this, password, key, keyPassword, loadKey, form] {
            const bool useKey = auth->currentData() == "key";
            for (auto *edit : {key, keyPassword}) {
                edit->setVisible(useKey);
                form->labelForField(edit)->setVisible(useKey);
            }
            loadKey->setVisible(useKey);
            password->setVisible(!useKey);
            form->labelForField(password)->setVisible(!useKey);
        };
        connect(auth, &QComboBox::currentIndexChanged, this, authVisibility);
        authVisibility();
        if (server["kind"] == "qwdtt") {
            add("dns1", "DNS 1");
            add("dns2", "DNS 2");
        }
        manual = new QCheckBox("Ручное управление портами", group);
        manual->setChecked(server["manualPorts"] == "true");
        form->addRow(manual);
        QStringList ports{"peerPort"};
        QStringList labels{"Порт DTLS"};
        if (server["kind"] == "qwdtt") {
            ports += {"wgPort", "directPort", "rawPort", "adminPort"};
            labels += {"Порт WireGuard", "Прямой порт", "Порт RAW", "Порт панели"};
        } else {
            ports.append("webPort");
            labels.append("Порт веб-панели");
        }
        for (int i = 0; i < ports.size(); ++i) {
            auto *edit = add(ports[i], labels[i]);
            edit->setEnabled(manual->isChecked());
            connect(manual, &QCheckBox::toggled, edit, &QWidget::setEnabled);
        }
        if (server["kind"] == "csqtt") {
            docker = new QCheckBox("Установить в Docker", group);
            docker->setChecked(server["docker"] == "true");
            form->addRow(docker);
            auto *web = new QPushButton("Открыть веб-панель", body);
            layout->addWidget(web);
            connect(web, &QPushButton::clicked, this, [this] {
                capture();
                QUrl url;
                url.setScheme("https");
                url.setHost(server["host"].toString());
                url.setPort(server["webPort"].toString().toInt());
                QDesktopServices::openUrl(url);
            });
        }
        auto *consoleGroup = new QGroupBox("Консоль", body);
        layout->addWidget(consoleGroup);
        auto *consoleLayout = new QVBoxLayout(consoleGroup);
        step = new QLabel("Готово к подключению", consoleGroup);
        consoleLayout->addWidget(step);
        progress = new QProgressBar(consoleGroup);
        progress->setValue(0);
        consoleLayout->addWidget(progress);
        console = new QPlainTextEdit(consoleGroup);
        console->setReadOnly(true);
        console->setMaximumBlockCount(4000);
        console->setMinimumHeight(180);
        consoleLayout->addWidget(console);
        auto *configGroup = new QGroupBox(server["kind"].toString() + " конфиг", body);
        layout->addWidget(configGroup);
        auto *configLayout = new QVBoxLayout(configGroup);
        configuration = new QPlainTextEdit(server["config"].toString(), configGroup);
        configuration->setReadOnly(true);
        configuration->setPlaceholderText("Конфиг появится после успешной установки");
        configLayout->addWidget(configuration);
        auto *copy = new QPushButton("Копировать конфиг", configGroup);
        configLayout->addWidget(copy);
        connect(copy, &QPushButton::clicked, this,
                [this] { QApplication::clipboard()->setText(configuration->toPlainText()); });
        auto *stop = new QPushButton("Остановить", this);
        outer->addWidget(stop);
        connect(stop, &QPushButton::clicked, task, &RelayTask::cancel);
        auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this);
        outer->addWidget(close);
        connect(close, &QDialogButtonBox::rejected, this, &QWidget::close);
    }
};
} // namespace

bool SaveVKRelayPreviews(const QString &directory) {
    if (directory.isEmpty())
        return true;
    QDir().mkpath(directory);
    const auto previousTheme = themeManager()->current_theme;
    bool saved = true;
    for (const QString theme : {QStringLiteral("flatgray"), QStringLiteral("qdarkstyle")}) {
        themeManager()->ApplyTheme(theme, true);
        const auto snapshot = [&](QWidget &widget, const QString &name) {
            widget.show();
            for (int i = 0; i < 6; ++i)
                QApplication::processEvents();
            saved = widget.grab().save(directory + "/vk-" + name + "-" + theme + ".png") && saved;
        };
        {
            DialogVKRelay dialog;
            snapshot(dialog, "account");
        }
        for (const QString kind : {QStringLiteral("qwdtt"), QStringLiteral("csqtt")}) {
            ServerEditor editor(freshServer(kind), [](QJsonObject) {}, nullptr);
            snapshot(editor, kind + "-deploy");
        }
        {
            EditQWDTT editor;
            editor.resize(760, 760);
            snapshot(editor, "qwdtt-manual");
        }
    }
    themeManager()->ApplyTheme(previousTheme, true);
    return saved;
}

DialogVKRelay::DialogVKRelay(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Настройки VK-релея");
    resize(860, 600);
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs);
    auto *accountPage = new QWidget(tabs);
    tabs->addTab(accountPage, "Аккаунт и хеши");
    auto *accountLayout = new QVBoxLayout(accountPage);
    auto *accountGroup = new QGroupBox("Аккаунт VK", accountPage);
    accountLayout->addWidget(accountGroup);
    auto *accountForm = new QVBoxLayout(accountGroup);
    auto *identity = new QLabel("Проверка аккаунта…", accountGroup);
    accountForm->addWidget(identity);
    auto *accountButtons = new QHBoxLayout;
    accountForm->addLayout(accountButtons);
    auto *link = new QPushButton("Привязать аккаунт VK", accountGroup);
    auto *unlink = new QPushButton("Отвязать аккаунт", accountGroup);
    accountButtons->addWidget(link);
    accountButtons->addWidget(unlink);
    unlink->setEnabled(false);
    auto *hashGroup = new QGroupBox("Хеши по умолчанию для новых профилей qWDTT", accountPage);
    accountLayout->addWidget(hashGroup);
    auto *hashForm = new QFormLayout(hashGroup);
    auto *source = new QComboBox(hashGroup);
    source->addItem("Вручную", "manual");
    source->addItem("Авто", "auto");
    autoEnabled(source, false);
    hashForm->addRow("Источник хешей", source);
    auto *settings = Configs::dataManager->settingsRepo.get();
    source->setCurrentIndex(settings->qwdtt_hash_source == "auto" ? 1 : 0);
    auto *count = new QSpinBox(hashGroup);
    count->setRange(1, 4);
    count->setValue(settings->qwdtt_auto_hash_count);
    hashForm->addRow("Количество хешей", count);
    auto *generate = new QPushButton("Создать хеши VK", hashGroup);
    hashForm->addRow(generate);
    generate->setEnabled(false);
    const auto sourceVisibility = [source, generate, count, hashForm] {
        const bool automatic = source->currentData() == "auto";
        generate->setVisible(automatic);
        count->setVisible(automatic);
        hashForm->labelForField(count)->setVisible(automatic);
    };
    connect(source, &QComboBox::currentIndexChanged, this, sourceVisibility);
    sourceVisibility();
    auto *status = new QLabel(this);
    status->setWordWrap(true);
    accountLayout->addStretch();
    auto *accountTask = new RelayTask(this);
    const auto infoHandler = [=](const QJsonObject &event) {
        if (event["type"] == "result") {
            const auto info = event["value"].toObject();
            const bool linked = info["linked"].toBool();
            identity->setText(linked ? info["name"].toString() : "Аккаунт не привязан");
            settings->vk_relay_linked = linked;
            autoEnabled(source, linked);
            unlink->setEnabled(linked);
            generate->setEnabled(linked);
            if (!linked)
                source->setCurrentIndex(0);
            settings->Save();
        }
        if (event["type"] == "progress" || event["type"] == "error")
            status->setText(event["text"].toString());
        if (event["type"] == "finished")
            link->setEnabled(true);
    };
    accountTask->start({{"operation", "vk-info"}}, infoHandler);
    connect(link, &QPushButton::clicked, this, [=] {
        link->setEnabled(false);
        accountTask->start({{"operation", "vk-login"}}, infoHandler);
    });
    connect(unlink, &QPushButton::clicked, this, [=, this] {
        if (QMessageBox::question(
                this, "Отвязать VK",
                "Все профили qWDTT с автоматическими хешами перейдут в ручной режим. Отвязать аккаунт?") !=
            QMessageBox::Yes)
            return;
        accountTask->start({{"operation", "secret-delete"}, {"id", "account"}}, [=](const QJsonObject &event) {
            if (event["type"] == "done") {
                if (!ResetQWDTTHashSources())
                    status->setText(
                        "Аккаунт отвязан, но не удалось сохранить часть профилей. Проверьте доступ к базе.");
                source->setCurrentIndex(0);
                autoEnabled(source, false);
                identity->setText("Аккаунт не привязан");
                unlink->setEnabled(false);
                generate->setEnabled(false);
            }
            if (event["type"] == "error")
                status->setText(event["text"].toString());
        });
    });
    connect(generate, &QPushButton::clicked, this, [=] {
        generate->setEnabled(false);
        accountTask->start({{"operation", "vk-hashes"}, {"count", count->value()}}, [=](const QJsonObject &event) {
            if (event["type"] == "result") {
                status->setText("Хеши готовы");
            }
            if (event["type"] == "error" || event["type"] == "progress")
                status->setText(event["text"].toString());
            if (event["type"] == "finished")
                generate->setEnabled(settings->vk_relay_linked);
        });
    });
    auto *storeTask = new RelayTask(this);
    auto servers = std::make_shared<QJsonArray>();
    auto lists = std::make_shared<QMap<QString, QListWidget *>>();
    auto pages = std::make_shared<QList<QWidget *>>();
    const auto refresh = [servers, lists] {
        for (auto it = lists->begin(); it != lists->end(); ++it) {
            it.value()->clear();
            for (const auto &entry : *servers) {
                const auto server = entry.toObject();
                if (server["kind"] != it.key())
                    continue;
                auto *item =
                    new QListWidgetItem(server["name"].toString() + " — " + server["host"].toString(), it.value());
                item->setData(Qt::UserRole, server["id"]);
            }
        }
    };
    const auto persist = [=](QJsonObject server) {
        bool found = false;
        for (int i = 0; i < servers->size(); ++i)
            if ((*servers)[i].toObject()["id"] == server["id"]) {
                (*servers)[i] = server;
                found = true;
                break;
            }
        if (!found)
            servers->append(server);
        refresh();
        storeTask->start({{"operation", "secret-set"},
                          {"id", "servers"},
                          {"value", QString::fromUtf8(QJsonDocument(*servers).toJson(QJsonDocument::Compact))}},
                         [=](const QJsonObject &event) {
                             if (event["type"] == "error")
                                 status->setText(event["text"].toString());
                         });
    };
    for (const auto &kind : {QString("qwdtt"), QString("csqtt")}) {
        auto *page = new QWidget(tabs);
        page->setEnabled(false);
        pages->append(page);
        tabs->addTab(page, kind == "qwdtt" ? "Деплой qWDTT" : "Деплой CSQTT");
        auto *pageLayout = new QVBoxLayout(page);
        auto *group = new QGroupBox("VPS для деплоя", page);
        pageLayout->addWidget(group);
        auto *groupLayout = new QVBoxLayout(group);
        auto *list = new QListWidget(group);
        groupLayout->addWidget(list);
        (*lists)[kind] = list;
        const auto open = [=, this](QJsonObject server) {
            auto *editor = new ServerEditor(server, persist, this);
            editor->setAttribute(Qt::WA_DeleteOnClose);
            editor->setWindowModality(Qt::WindowModal);
            editor->show();
        };
        connect(list, &QListWidget::itemDoubleClicked, this, [=](QListWidgetItem *item) {
            for (const auto &entry : *servers)
                if (entry.toObject()["id"] == item->data(Qt::UserRole).toJsonValue()) {
                    open(entry.toObject());
                    break;
                }
        });
        auto *buttons = new QHBoxLayout;
        pageLayout->addLayout(buttons);
        auto *add = new QPushButton("Добавить VPS для деплоя", page);
        auto *remove = new QPushButton("Удалить из списка", page);
        buttons->addWidget(add);
        buttons->addWidget(remove);
        connect(add, &QPushButton::clicked, this, [=] {
            const auto server = freshServer(kind);
            persist(server);
            open(server);
        });
        connect(remove, &QPushButton::clicked, this, [=, this] {
            auto *item = list->currentItem();
            if (!item)
                return;
            if (QMessageBox::question(this, "Удалить из списка",
                                      "Удалить запись VPS? Установленный сервис на сервере останется.") !=
                QMessageBox::Yes)
                return;
            const auto id = item->data(Qt::UserRole).toJsonValue();
            for (int i = 0; i < servers->size(); ++i)
                if ((*servers)[i].toObject()["id"] == id) {
                    servers->removeAt(i);
                    break;
                }
            refresh();
            storeTask->start({{"operation", "secret-set"},
                              {"id", "servers"},
                              {"value", QString::fromUtf8(QJsonDocument(*servers).toJson(QJsonDocument::Compact))}},
                             [=](const QJsonObject &event) {
                                 if (event["type"] == "error")
                                     status->setText(event["text"].toString());
                             });
        });
    }
    storeTask->start({{"operation", "secret-get"}, {"id", "servers"}}, [=](const QJsonObject &event) {
        if (event["type"] == "result") {
            const auto raw = event["value"].toString().toUtf8();
            const auto doc = QJsonDocument::fromJson(raw);
            if (!raw.isEmpty() && !doc.isArray()) {
                status->setText("Не удалось прочитать список серверов");
                return;
            }
            *servers = doc.array();
            refresh();
            for (auto *page : *pages)
                page->setEnabled(true);
        }
        if (event["type"] == "error")
            status->setText(event["text"].toString());
    });
    layout->addWidget(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [=, this] {
        settings->qwdtt_hash_source = source->currentData().toString();
        settings->qwdtt_auto_hash_count = count->value();
        settings->Save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::finished, accountTask, &RelayTask::cancel);
}
