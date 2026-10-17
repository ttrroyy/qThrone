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
#include <QElapsedTimer>
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
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

namespace {
// Like the original app, a deployment's qWDTT link names no tunnel mode: the client's
// default (WG) applies. Links stored by earlier versions said RAW; they read the same now.
QString withoutTunnelMode(QString link) {
    if (!link.startsWith("qwdtt://config", Qt::CaseInsensitive)) return link;
    static const QRegularExpression mode(R"(([?&])mode=[^&#]*&?)");
    static const QRegularExpression dangling(R"([?&](#|$))");
    link.replace(mode, "\\1");
    link.replace(dangling, "\\1");
    return link;
}
}

RelayTask::RelayTask(QObject *parent) : QObject(parent) {
    connect(&process, &QProcess::started, this, [this] { process.write(requestBytes); });
    connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
        pending += process.readAllStandardOutput();
        while (pending.contains('\n')) {
            const int end = pending.indexOf('\n');
            const auto line = pending.left(end);
            pending.remove(0, end + 1);
            const auto event = QJsonDocument::fromJson(line).object();
            if (event["type"] == "vk-confirm") {
                approve(QMessageBox::question(qobject_cast<QWidget *>(this->parent()), "Подтверждение VK",
                                               event["text"].toString(), QMessageBox::Yes | QMessageBox::No,
                                               QMessageBox::No) == QMessageBox::Yes);
                continue;
            }
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
    QStringList result;
    QByteArray pending;
    QElapsedTimer elapsed;
    elapsed.start();
    while (true) {
        process.waitForReadyRead(100);
        pending += process.readAllStandardOutput();
        while (pending.contains('\n')) {
            const auto end = pending.indexOf('\n');
            const auto event = QJsonDocument::fromJson(pending.left(end)).object();
            pending.remove(0, end + 1);
            if (event["type"] == "vk-confirm") {
                bool accepted = false;
                const auto confirm = [&] {
                    accepted = QMessageBox::question(GetMessageBoxParent(), "Подтверждение VK",
                                                      event["text"].toString(), QMessageBox::Yes | QMessageBox::No,
                                                      QMessageBox::No) == QMessageBox::Yes;
                };
                auto *app = QCoreApplication::instance();
                if (app && app->thread() == QThread::currentThread())
                    confirm();
                else if (app)
                    QMetaObject::invokeMethod(app, confirm, Qt::BlockingQueuedConnection);
                process.write(accepted ? "approve\n" : "reject\n");
                // The browser OAuth flow has its own five-minute timeout.
                elapsed.restart();
            }
            if (event["type"] == "result") {
                const auto value = event["value"];
                const auto hashes = value.isObject() ? value.toObject()["hashes"].toArray() : value.toArray();
                for (const auto &hash : hashes)
                    result.append(hash.toString());
            }
            if (event["type"] == "error")
                *error = event["text"].toString();
        }
        if (process.state() == QProcess::NotRunning)
            break;
        if (elapsed.elapsed() > 360000) {
            process.kill();
            process.waitForFinished(5000);
            *error = QStringLiteral("VK: превышено время ожидания");
            return {};
        }
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
            // The server is bound to this computer, like the original apps bind it to the phone:
            // the deployment link names no device and new profiles take this ID.
            {"deviceId", Configs::RelayDeviceID()},
            {"adminToken", QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-') +
                               QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-')}};
}
class ServerEditor : public QDialog {
    friend bool ::SaveVKRelayPreviews(const QString &directory);
    QJsonObject server;
    QJsonObject savedState;
    QMap<QString, QLineEdit *> fields;
    QComboBox *auth;
    QCheckBox *manual;
    QCheckBox *docker = nullptr;
    QPlainTextEdit *console;
    QPlainTextEdit *configuration;
    QProgressBar *progress;
    QLabel *step;
    RelayTask *task;
    QPushButton *stopButton;
    QPushButton *saveButton;
    QPushButton *copyButton;
    bool busy = false;
    void setBusy(bool active) {
        busy = active;
        for (auto *widget : controls)
            widget->setEnabled(!active);
        stopButton->setEnabled(active);
        updateDirty();
    }
    QJsonObject editableState() const {
        auto state = server;
        for (const auto &key : {"console", "status", "progress", "config", "hostKey", "adminPin"})
            state.remove(key);
        return state;
    }
    void updateDirty() {
        capture();
        saveButton->setEnabled(!busy && editableState() != savedState);
    }
    void applyConfiguration(const QJsonValue &value) {
        server["config"] = withoutTunnelMode(value.toString());
        configuration->setPlainText(server["config"].toString());
        rememberConsole();
    }
    QList<QWidget *> controls;
    std::function<void(QJsonObject)> save;
    std::function<void(QJsonObject)> checkpoint;
    void rememberConsole(bool persist = true) {
        server["console"] = console->toPlainText().right(200000);
        server["status"] = step->text();
        server["progress"] = QString::number(progress->value());
        if (persist && checkpoint)
            checkpoint(server);
    }
    void capture() {
        const auto oldHost = server["host"].toString();
        const auto oldPort = server["sshPort"].toString();
        for (auto it = fields.begin(); it != fields.end(); ++it)
            if (it.key() != "key")
                server[it.key()] = it.value()->text();
        server["auth"] = auth->currentData().toString();
        if (server["auth"] == "password")
            server["sudoPassword"] = "";
        else
            server["sshPassword"] = "";
        for (const auto &key : {"host", "user", "sshPort"})
            server[key] = server[key].toString().trimmed();
        if (server["host"].toString() != oldHost || server["sshPort"].toString() != oldPort) {
            server["hostKey"] = "";
            server["adminPin"] = "";
        }
        server["manualPorts"] = manual->isChecked() ? "true" : "false";
        if (docker)
            server["docker"] = docker->isChecked() ? "true" : "false";
    }
    void secrets() {
        QDialog dialog(this);
        dialog.setWindowTitle(server["kind"] == "qwdtt" ? "Секреты (бот, пароли)" : "Авторизация");
        dialog.resize(640, 240);
        dialog.setMinimumSize(560, 210);
        auto *layout = new QVBoxLayout(&dialog);
        auto *form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        layout->addLayout(form);
        QMap<QString, QLineEdit *> edits;
        QList<QPair<QString, QString>> keys{{"password", "Пароль туннеля"}};
        if (server["kind"] == "qwdtt")
            keys += {{"botToken", "Токен Telegram-бота (необязательно)"},
                     {"adminId", "Telegram ID администратора (необязательно)"}};
        else
            keys += {{"webUser", "Логин веб-панели"}, {"webPassword", "Пароль веб-панели"}};
        for (const auto &[key, label] : keys) {
            auto *edit = new QLineEdit(server[key].toString(), &dialog);
            edit->setObjectName("relaySecret_" + key);
            if (key.contains("Token") || key.contains("assword"))
                edit->setEchoMode(QLineEdit::Password);
            if (key == "adminId")
                edit->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]*"), edit));
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
            updateDirty();
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
        console->clear();
        progress->setValue(0);
        setBusy(true);
        auto requestServer = server;
        for (const auto &key : {"console", "status", "progress"})
            requestServer.remove(key);
        task->start({{"operation", uninstall ? "uninstall" : "deploy"}, {"server", requestServer}},
                    [this](const QJsonObject &event) {
                        const auto type = event["type"].toString();
                        const auto text = event["text"].toString();
                        if (type == "trust")
                            task->approve(QMessageBox::question(this, "SSH-ключ VPS",
                                                                "Первое подключение к " + server["host"].toString() +
                                                                    ".\n\n" + text + "\n\nДоверять этому ключу?") ==
                                          QMessageBox::Yes);
                        if (type == "host-key") {
                            server["hostKey"] = text;
                        }
                        if (type == "admin-pin") {
                            server["adminPin"] = text;
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
                            applyConfiguration(event["value"]);
                        }
                        if (type == "error") {
                            step->setText(text);
                            console->appendPlainText("Ошибка: " + text);
                        }
                        if (type == "finished") {
                            setBusy(false);
                            rememberConsole();
                            updateDirty();
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
        rememberConsole();
        QDialog::closeEvent(event);
    }

  public:
    ServerEditor(QJsonObject value, std::function<void(QJsonObject)> callback, QWidget *parent,
                 std::function<void(QJsonObject)> consoleCallback = {})
        : QDialog(parent), server(std::move(value)), save(std::move(callback)), checkpoint(std::move(consoleCallback)) {
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
        secretsButton->setObjectName("relaySecrets");
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
            edit->setObjectName("relayField_" + key);
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
        auth->setObjectName("relayAuth");
        auth->addItem("Пароль", "password");
        auth->addItem("SSH-ключ", "key");
        auth->setCurrentIndex(server["auth"] == "key" ? 1 : 0);
        form->addRow("Вход на сервер", auth);
        auto *password = add("sshPassword", "Пароль SSH", true);
        auto *key = add("key", "SSH-ключ");
        key->setReadOnly(true);
        key->setText(server["key"].toString().isEmpty() ? "Не привязан" : "Привязан");
        key->setToolTip(server["keyName"].toString());
        auto *loadKey = new QPushButton("Загрузить SSH-ключ…", group);
        form->addRow("", loadKey);
        auto *keyPassword = add("keyPassword", "Пароль SSH-ключа", true);
        auto *sudoPassword = add("sudoPassword", "Пароль sudo (если требуется)", true);
        connect(loadKey, &QPushButton::clicked, this, [this, key] {
            const auto path = QFileDialog::getOpenFileName(this, "SSH-ключ");
            if (!path.isEmpty()) {
                const auto bytes = ReadFileText(path);
                if (bytes.isEmpty()) {
                    QMessageBox::warning(this, "SSH-ключ", "Не удалось прочитать файл");
                    return;
                }
                server["key"] = bytes;
                server["keyName"] = QFileInfo(path).fileName();
                key->setText("Привязан");
                key->setToolTip(server["keyName"].toString());
            }
        });
        const auto authVisibility = [this, password, sudoPassword, key, keyPassword, loadKey, form] {
            const bool useKey = auth->currentData() == "key";
            for (auto *edit : {key, keyPassword, sudoPassword}) {
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
        manual->setObjectName("relayManualPorts");
        manual->setChecked(server["manualPorts"] == "true");
        form->addRow(manual);
        QStringList ports{"peerPort"};
        QStringList labels{server["kind"] == "qwdtt" ? "Порт DTLS" : "Порт туннеля"};
        if (server["kind"] == "qwdtt") {
            ports += {"wgPort", "directPort", "rawPort"};
            labels += {"Порт WireGuard", "Прямой порт", "Порт RAW"};
        } else {
            ports.append("webPort");
            labels.append("Порт веб-панели");
        }
        for (int i = 0; i < ports.size(); ++i) {
            auto *edit = add(ports[i], labels[i]);
            const auto visible = [edit, form](bool checked) {
                edit->setVisible(checked);
                form->labelForField(edit)->setVisible(checked);
            };
            visible(manual->isChecked());
            connect(manual, &QCheckBox::toggled, edit, visible);
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
        step = new QLabel(server["status"].toString("Готово к подключению"), consoleGroup);
        consoleLayout->addWidget(step);
        progress = new QProgressBar(consoleGroup);
        progress->setValue(qBound(0, server["progress"].toString().toInt(), 100));
        consoleLayout->addWidget(progress);
        console = new QPlainTextEdit(consoleGroup);
        console->setObjectName("relayConsole");
        console->setReadOnly(true);
        console->setMaximumBlockCount(4000);
        console->setMinimumHeight(180);
        console->setPlainText(server["console"].toString());
        consoleLayout->addWidget(console);
        stopButton = new QPushButton("Остановить", consoleGroup);
        stopButton->setObjectName("relayStop");
        stopButton->setEnabled(false);
        consoleLayout->addWidget(stopButton);
        connect(stopButton, &QPushButton::clicked, task, &RelayTask::cancel);
        auto *configGroup = new QGroupBox(server["kind"].toString() + " конфиг", body);
        layout->addWidget(configGroup);
        auto *configLayout = new QVBoxLayout(configGroup);
        configuration = new QPlainTextEdit(withoutTunnelMode(server["config"].toString()), configGroup);
        configuration->setReadOnly(true);
        configuration->setPlaceholderText("Конфиг появится после успешной установки");
        configLayout->addWidget(configuration);
        copyButton = new QPushButton("Копировать конфиг", configGroup);
        copyButton->setEnabled(!configuration->toPlainText().isEmpty());
        configLayout->addWidget(copyButton);
        connect(configuration, &QPlainTextEdit::textChanged, this,
                [this] { copyButton->setEnabled(!configuration->toPlainText().isEmpty()); });
        connect(copyButton, &QPushButton::clicked, this,
                [this] { QApplication::clipboard()->setText(configuration->toPlainText()); });
        auto *buttons = new QHBoxLayout;
        buttons->addStretch();
        saveButton = new QPushButton("Сохранить", this);
        saveButton->setObjectName("relaySave");
        buttons->addWidget(saveButton);
        outer->addLayout(buttons);
        controls.append(saveButton);
        connect(saveButton, &QPushButton::clicked, this, [this] {
            capture();
            rememberConsole(false);
            if (!QRegularExpression("^[A-Za-z0-9.:_-]+$").match(server["host"].toString()).hasMatch() ||
                !QRegularExpression("^[A-Za-z0-9_.-]+$").match(server["user"].toString()).hasMatch()) {
                QMessageBox::warning(this, "VPS", "Укажите IP или домен без схемы и порта, а также логин SSH");
                return;
            }
            save(server);
            savedState = editableState();
            updateDirty();
        });
        capture();
        savedState = editableState();
        saveButton->setEnabled(false);
        for (auto *edit : fields)
            connect(edit, &QLineEdit::textChanged, this, [this] { updateDirty(); });
        connect(auth, &QComboBox::currentIndexChanged, this, [this] { updateDirty(); });
        connect(manual, &QCheckBox::toggled, this, [this] { updateDirty(); });
        if (docker)
            connect(docker, &QCheckBox::toggled, this, [this] { updateDirty(); });
    }
};
} // namespace

bool SaveVKRelayPreviews(const QString &directory) {
    int saves = 0;
    {
        ServerEditor draft(freshServer("qwdtt"), [&](QJsonObject) { ++saves; }, nullptr);
        draft.close();
    }
    if (saves != 0)
        return false;
    {
        auto value = freshServer("csqtt");
        value["host"] = "203.0.113.1";
        value["hostKey"] = "old-fingerprint";
        value["sshPassword"] = "password-login";
        value["sudoPassword"] = "key-sudo";
        value["console"] = "Результат предыдущего деплоя";
        QJsonObject stored;
        ServerEditor draft(
            value,
            [&](QJsonObject item) {
                ++saves;
                stored = item;
            },
            nullptr);
        auto *saveButton = draft.findChild<QPushButton *>("relaySave");
        auto *stop = draft.findChild<QPushButton *>("relayStop");
        if (!saveButton || !stop || saveButton->isEnabled() || stop->isEnabled() ||
            !qobject_cast<QGroupBox *>(stop->parentWidget()))
            return false;
        auto *auth = draft.findChild<QComboBox *>("relayAuth");
        auto *sudoPassword = draft.findChild<QLineEdit *>("relayField_sudoPassword");
        auto *sshPassword = draft.findChild<QLineEdit *>("relayField_sshPassword");
        auto *ports = draft.findChild<QCheckBox *>("relayManualPorts");
        auto *peerPort = draft.findChild<QLineEdit *>("relayField_peerPort");
        auto *key = draft.findChild<QLineEdit *>("relayField_key");
        auto *keyPassword = draft.findChild<QLineEdit *>("relayField_keyPassword");
        auto *form = qobject_cast<QFormLayout *>(key->parentWidget()->layout());
        if (!form || form->indexOf(auth) >= form->indexOf(key) || form->indexOf(key) >= form->indexOf(keyPassword) ||
            form->indexOf(keyPassword) >= form->indexOf(sudoPassword))
            return false;
        auto *console = draft.findChild<QPlainTextEdit *>("relayConsole");
        if (!console || console->toPlainText() != value["console"].toString())
            return false;
        console->setPlainText("Новый результат деплоя");
        draft.findChild<QLineEdit *>("relayField_name")->setText("Изменённый сервер");
        if (!saveButton->isEnabled())
            return false;
        draft.setBusy(true);
        if (!stop->isEnabled() || saveButton->isEnabled())
            return false;
        draft.setBusy(false);
        if (stop->isEnabled() || !saveButton->isEnabled())
            return false;
        if (!sudoPassword->isHidden() || sshPassword->isHidden() || !peerPort->isHidden())
            return false;
        saveButton->click();
        if (saveButton->isEnabled())
            return false;
        if (!stored["sudoPassword"].toString().isEmpty())
            return false;
        auth->setCurrentIndex(1);
        ports->setChecked(true);
        if (sudoPassword->isHidden() || !sshPassword->isHidden() || peerPort->isHidden())
            return false;
        draft.findChild<QLineEdit *>("relayField_host")->setText("203.0.113.2");
        saveButton->click();
        if (!stored["sshPassword"].toString().isEmpty() || !stored["hostKey"].toString().isEmpty())
            return false;
        draft.close();
        ServerEditor reopened(stored, [&](QJsonObject) { ++saves; }, nullptr);
        if (reopened.findChild<QPlainTextEdit *>("relayConsole")->toPlainText() != "Новый результат деплоя")
            return false;
        reopened.close();
    }
    if (saves != 2)
        return false;
    {
        auto stored = freshServer("qwdtt");
        stored["host"] = "203.0.113.10";
        stored["config"] = "qwdtt://old-deployment";
        int explicitSaves = 0;
        ServerEditor editor(
            stored, [&](QJsonObject) { ++explicitSaves; }, nullptr,
            [&](QJsonObject state) {
                for (const auto &key : {"console", "status", "progress", "config"})
                    stored[key] = state[key];
            });
        editor.applyConfiguration(QString());
        editor.close();
        ServerEditor reopened(stored, [](QJsonObject) {}, nullptr);
        if (explicitSaves != 0 || reopened.copyButton->isEnabled() || !stored["config"].toString().isEmpty() ||
            !reopened.configuration->toPlainText().isEmpty())
            return false;
    }
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
            saved = dialog.size() == QSize(860, 600) && dialog.findChildren<QDialogButtonBox *>().isEmpty() && saved;
            for (auto *edit : dialog.findChildren<QLineEdit *>())
                if (!qobject_cast<QSpinBox *>(edit->parentWidget()))
                    saved = false;
            auto *tabs = dialog.findChild<QTabWidget *>();
            for (int i = 1; i < 3; ++i) {
                tabs->setCurrentIndex(i);
                snapshot(dialog, i == 1 ? "qwdtt-list" : "csqtt-list");
                saved = dialog.size() == QSize(860, 600) && saved;
            }
        }
        for (const QString kind : {QStringLiteral("qwdtt"), QStringLiteral("csqtt")}) {
            ServerEditor editor(freshServer(kind), [](QJsonObject) {}, nullptr);
            snapshot(editor, kind + "-deploy");
            editor.findChild<QComboBox *>("relayAuth")->setCurrentIndex(1);
            snapshot(editor, kind + "-ssh-key");
            QTimer::singleShot(0, &editor, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog) {
                    saved = false;
                    return;
                }
                snapshot(*dialog, kind + "-secrets");
                if (dialog->findChild<QLineEdit *>("relaySecret_adminToken") ||
                    dialog->findChild<QLineEdit *>("relaySecret_deviceId"))
                    saved = false;
                dialog->reject();
            });
            editor.findChild<QPushButton *>("relaySecrets")->click();
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
    const auto saveDefaults = [settings, source, count] {
        settings->qwdtt_hash_source = source->currentData().toString();
        settings->qwdtt_auto_hash_count = count->value();
        settings->Save();
    };
    connect(source, &QComboBox::currentIndexChanged, this, saveDefaults);
    connect(count, &QSpinBox::valueChanged, this, saveDefaults);
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
        if (event["type"] == "cancelled")
            status->clear();
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
    const auto checkpoint = [=](const QJsonObject &state) {
        for (const auto &entry : *servers) {
            auto stored = entry.toObject();
            if (stored["id"] != state["id"])
                continue;
            for (const auto &key : {"console", "status", "progress"})
                stored[key] = state[key];
            const bool sameEndpoint = stored["host"] == state["host"] && stored["sshPort"] == state["sshPort"];
            if (sameEndpoint || state["config"].toString().isEmpty())
                stored["config"] = state["config"];
            if (sameEndpoint)
                for (const auto &key : {"hostKey", "adminPin"})
                    stored[key] = state[key];
            persist(stored);
            return;
        }
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
            auto *editor = new ServerEditor(server, persist, this, checkpoint);
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
    connect(this, &QDialog::finished, accountTask, &RelayTask::cancel);
}
