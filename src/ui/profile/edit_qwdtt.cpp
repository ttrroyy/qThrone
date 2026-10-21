#include "include/ui/profile/edit_qwdtt.h"
#include "include/configs/outbounds/qwdtt.h"
#include "include/global/Configs.hpp"
#include "include/ui/setting/dialog_vk_relay.h"
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

EditQWDTT::EditQWDTT(QWidget *parent) : QWidget(parent) {
    form = new QFormLayout(this);
    mode = new QComboBox(this);
    mode->addItem("RAW", "raw");
    mode->addItem("WG", "wg");
    form->addRow(tr("Tunnel mode"), mode);
    password = new QLineEdit(this);
    password->setEchoMode(QLineEdit::Normal);
    form->addRow(tr("Password"), password);
    hashSource = new QComboBox(this);
    hashSource->addItem(QStringLiteral("Вручную"), "manual");
    hashSource->addItem(QStringLiteral("Авто"), "auto");
    hashSource->setObjectName("qwdttHashSource");
    qobject_cast<QStandardItemModel *>(hashSource->model())
        ->item(1)
        ->setEnabled(Configs::dataManager->settingsRepo->vk_relay_linked);
    form->addRow(QStringLiteral("Источник хешей"), hashSource);
    autoHashCount = new QSpinBox(this);
    autoHashCount->setRange(1, 4);
    form->addRow(QStringLiteral("Количество хешей"), autoHashCount);
    for (int i = 0; i < 4; ++i) {
        hashes[i] = new QLineEdit(this);
        hashes[i]->setObjectName(QString("qwdttHash%1").arg(i + 1));
        hashes[i]->setPlaceholderText(tr("Call hash or VK call link"));
        hashRows[i] = new QWidget(this);
        auto *row = new QHBoxLayout(hashRows[i]);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(hashes[i]);
        auto *copy = new QPushButton(QStringLiteral("Копировать"), hashRows[i]);
        copy->setObjectName(QString("qwdttHashCopy%1").arg(i + 1));
        copy->setEnabled(false);
        row->addWidget(copy);
        hashStatus[i] = new QLabel(hashRows[i]);
        row->addWidget(hashStatus[i]);
        connect(copy, &QPushButton::clicked, this, [this, i] {
            QApplication::clipboard()->setText(hashes[i]->text().trimmed());
        });
        connect(hashes[i], &QLineEdit::textChanged, this, [this, i, copy] {
            copy->setEnabled(!hashes[i]->text().trimmed().isEmpty());
            hashStatus[i]->clear();
            stopHashCheck();
        });
        form->addRow(tr("VK hash %1").arg(i + 1), hashRows[i]);
    }
    hashProcess = new QProcess(this);
    checkHashes = new QPushButton(QStringLiteral("Проверить хеши"), this);
    checkHashes->setObjectName("qwdttCheckHashes");
    form->addRow(checkHashes);
    auto *timeout = new QTimer(this);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, this, [this] {
        stopHashCheck();
        for (int slot : checkingSlots)
            if (hashStatus[slot]->text() == "Остановлено") hashStatus[slot]->setText("Время проверки истекло");
    });
    connect(hashProcess, &QProcess::readyReadStandardOutput, this, &EditQWDTT::consumeHashCheckOutput);
    // Diagnostic logs can contain URLs/captcha tokens; only structured results reach the UI.
    connect(hashProcess, &QProcess::readyReadStandardError, this, [this] {
        const auto text = QString::fromUtf8(hashProcess->readAllStandardError()).toLower();
        if (activeCheckingSlot>=0 && !completedHashChecks.contains(activeCheckingSlot) && !hashCheckStopped &&
            (text.contains("[captcha]") || text.contains("[капча]"))) {
            hashStatus[activeCheckingSlot]->setText("Решается капча…");
            hashStatus[activeCheckingSlot]->setToolTip("Авторизация VK ожидает завершения автоматического или ручного решения капчи.");
        }
        if (text.contains("[check] config:")) hashCheckFailure = "Неверные параметры проверки";
        else if (text.contains("permission denied")) hashCheckFailure = "Нет доступа к файлам проверки";
        else if (text.contains("cannot load vk captcha") || text.contains("executable file not found")) hashCheckFailure = "Не удалось запустить браузер для капчи";
    });
    const auto finishedCheck = [this, timeout] {
        consumeHashCheckOutput();
        timeout->stop();
        checkHashes->setText("Проверить хеши");
        checkHashes->setEnabled(true);
        for (int i : checkingSlots)
            if (!completedHashChecks.contains(i))
                hashStatus[i]->setText(hashCheckStopped ? "Остановлено" : hashCheckFailure.isEmpty() ? "Нет ответа" : hashCheckFailure);
        hashCheckDirectory.reset();
    };
    connect(hashProcess, &QProcess::finished, this, finishedCheck);
    connect(hashProcess, &QProcess::errorOccurred, this, [=](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { hashCheckFailure = "Не удалось запустить проверку"; finishedCheck(); }
    });
    connect(checkHashes, &QPushButton::clicked, this, [this, timeout] {
        if (hashProcess->state() != QProcess::NotRunning) { stopHashCheck(); return; }
        QJsonArray values;
        checkingSlots.clear();
        completedHashChecks.clear();
        activeCheckingSlot = -1;
        for (int i = 0; i < 4; ++i) {
            const auto normalized = Configs::qwdtt::NormalizeHashes({hashes[i]->text()});
            hashStatus[i]->clear();
            if (!normalized.isEmpty()) { values.append(normalized.first()); checkingSlots.append(i); }
        }
        if (values.isEmpty()) { MessageBoxWarning("qWDTT", "Укажите хеши для проверки."); return; }
        QString executable = "qwdtt";
#ifdef Q_OS_WIN
        executable += ".exe";
#endif
        executable = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(executable);
        if (!QFileInfo(executable).isExecutable()) { MessageBoxWarning("qWDTT", "Клиент проверки qWDTT не найден."); return; }
        hashCheckDirectory = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/qthrone-hash-check-XXXXXX");
        QFile config(hashCheckDirectory->filePath("check.json"));
        if (!hashCheckDirectory->isValid() || !config.open(QIODevice::WriteOnly)) {
            MessageBoxWarning("qWDTT", "Не удалось создать файл проверки."); hashCheckDirectory.reset(); return;
        }
        config.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        const auto data = QJsonDocument(QJsonObject{{"hashes", values}, {"captcha_mode", captcha->currentData().toString()},
            {"vk_anon_path", authorization->currentData().toString()}, {"go_dns", dns->currentText()},
            {"device_id", device->text()}}).toJson(QJsonDocument::Compact);
        if (config.write(data) != data.size()) { config.close(); hashCheckDirectory.reset(); return; }
        config.close();
        hashCheckStopped = false;
        hashCheckFailure.clear();
        hashCheckOutput.clear();
        checkHashes->setText("СТОП");
        hashProcess->setWorkingDirectory(hashCheckDirectory->path());
        hashProcess->start(executable, {"-hash-check-config", config.fileName()});
        timeout->start(qMax(90000, int(values.size()) * 100000));
    });
    rawPort = new QSpinBox(this);
    rawPort->setRange(1, 65535);
    rawPort->setValue(56003);
    form->addRow(tr("RAW server port"), rawPort);
    rawPort->setToolTip(tr("qWDTT RAW listener, normally 56003. The main server port is used by WG."));
    workers = new QComboBox(this);
    workers->setObjectName("qwdttWorkers");
    const auto updateSource = [this] {
        const bool automatic = hashSource->currentData() == "auto";
        stopHashCheck();
        for (int i = 0; i < 4; ++i) {
            hashes[i]->setVisible(!automatic);
            hashRows[i]->setVisible(!automatic);
            form->labelForField(hashRows[i])->setVisible(!automatic);
        }
        checkHashes->setVisible(!automatic);
        autoHashCount->setVisible(automatic);
        form->labelForField(autoHashCount)->setVisible(automatic);
        updateWorkers();
    };
    connect(hashSource, &QComboBox::currentIndexChanged, this, updateSource);
    connect(autoHashCount, &QSpinBox::valueChanged, this, [this] { updateWorkers(); });
    for (auto *field : hashes)
        connect(field, &QLineEdit::textChanged, this, [this] { updateWorkers(); });
    updateWorkers(9);
    updateSource();
    form->addRow(tr("Workers"), workers);
    transport = new QComboBox(this);
    transport->addItems({"UDP", "TCP"});
    transport->setCurrentIndex(1);
    form->addRow(tr("TURN transport"), transport);
    authorization = new QComboBox(this);
    authorization->setObjectName("qwdttAuthorization");
    authorization->addItem(tr("VK call"), "vkcalls");
    authorization->addItem(tr("Captcha"), "legacy");
    form->addRow(tr("VK authorization"), authorization);
    captcha = new QComboBox(this);
    captcha->setObjectName("qwdttCaptcha");
    captcha->addItem(tr("Automatic, with browser fallback"), "auto");
    captcha->addItem(tr("Browser window"), "wv");
    captcha->addItem(tr("Automatic"), "rjs");
    form->addRow(tr("Captcha handling"), captcha);
    obfs = new QComboBox(this);
    obfs->addItem(tr("Audio"), "audio");
    obfs->addItem(tr("Video"), "video");
    form->addRow(tr("Obfuscation"), obfs);
    dns = new QComboBox(this);
    dns->addItems({"yandex", "cloudflare", "google", "doh-yandex", "doh-cloudflare", "doh-google"});
    dns->setEditable(true);
    form->addRow(tr("VK DNS"), dns);
    device = new QLineEdit(this);
    form->addRow(tr("Device ID"), device);
    device->setToolTip(tr("Generated automatically. Keep it unchanged for passwords bound to a device."));
    auto *accountTask = new RelayTask(this);
    accountTask->start({{"operation", "vk-info"}}, [this](const QJsonObject &event) {
        if (event["type"] == "result") {
            const bool linked = event["value"].toObject()["linked"].toBool();
            qobject_cast<QStandardItemModel *>(hashSource->model())->item(1)->setEnabled(linked);
            if (!linked)
                hashSource->setCurrentIndex(0);
        }
    });
    connect(mode, &QComboBox::currentIndexChanged, this,
            [this](int) { rawPort->setEnabled(mode->currentData() == "raw"); });
}

void EditQWDTT::consumeHashCheckOutput() {
    hashCheckOutput += hashProcess->readAllStandardOutput();
    while (hashCheckOutput.contains('\n')) {
        const auto end = hashCheckOutput.indexOf('\n');
        const auto parts = QString::fromUtf8(hashCheckOutput.left(end)).trimmed().split('|');
        hashCheckOutput.remove(0, end + 1);
        if (hashCheckStopped || parts.size() < 3 ||
            (parts[0] != "HASH_CHECK_START" && parts[0] != "HASH_CHECK")) continue;
        const int order = parts[1].toInt() - 1;
        if (order < 0 || order >= checkingSlots.size()) continue;
        auto *label = hashStatus[checkingSlots[order]];
        if (parts[0] == "HASH_CHECK_START") {
            activeCheckingSlot = checkingSlots[order];
            label->setText("Проверяется…");
        }
        else if (parts.size() >= 5) {
            const QString status = parts[3] == "ok" ? "Работает" : parts[3] == "dead" ? "Закрыт" :
                parts[3] == "captcha" ? "Капча" : parts[3] == "limited" ? "Лимит VK" :
                parts[3] == "network" ? "Сетевая ошибка" : "Ошибка";
            completedHashChecks.insert(checkingSlots[order]);
            label->setText(status);
            label->setToolTip(parts.mid(4).join('|'));
        }
    }
}

void EditQWDTT::stopHashCheck() {
    if (!hashProcess || hashProcess->state() == QProcess::NotRunning || hashCheckStopped) return;
    hashCheckStopped = true;
    hashProcess->write("STOP\n");
    checkHashes->setText("Проверить хеши");
    checkHashes->setEnabled(false);
    for (int i : checkingSlots) {
        if (!completedHashChecks.contains(i)) hashStatus[i]->setText("Остановлено");
    }
    QTimer::singleShot(1000, hashProcess, [this] {
        if (hashCheckStopped && hashProcess->state() != QProcess::NotRunning) hashProcess->kill();
    });
}

EditQWDTT::~EditQWDTT() {
    hashProcess->disconnect(this);
    if (hashProcess->state() != QProcess::NotRunning) {
        hashProcess->write("STOP\n");
        if (!hashProcess->waitForFinished(1000)) { hashProcess->kill(); hashProcess->waitForFinished(1000); }
    }
}

void EditQWDTT::onStart(std::shared_ptr<Configs::Profile> profile) {
    ent = std::move(profile);
    auto *o = dynamic_cast<Configs::qwdtt *>(ent->outbound.get());
    if (!o)
        return;
    mode->setCurrentIndex(o->mode == "wg" ? 1 : 0);
    password->setText(o->password);
    autoHashCount->setValue(o->autoHashCount);
    hashSource->setCurrentIndex(o->hashSource == "auto" ? 1 : 0);
    for (int i = 0; i < 4; ++i)
        hashes[i]->setText(i < o->hashes.size() ? o->hashes[i] : QString());
    rawPort->setValue(o->rawPort);
    rawPort->setEnabled(o->mode == "raw");
    updateWorkers(o->workers);
    transport->setCurrentIndex(o->turnTCP ? 1 : 0);
    authorization->setCurrentIndex(authorization->findData(o->vkAnonPath));
    captcha->setCurrentIndex(captcha->findData(o->captchaMode));
    obfs->setCurrentIndex(o->obfs == "video" ? 1 : 0);
    dns->setCurrentText(o->goDNS);
    device->setText(o->deviceID);
}

bool EditQWDTT::onEnd() {
    auto *o = dynamic_cast<Configs::qwdtt *>(ent->outbound.get());
    if (!o)
        return false;
    QStringList values;
    for (auto *field : hashes)
        values.append(field->text());
    values = Configs::qwdtt::NormalizeHashes(values);
    const auto invalid = [this](const QString &message) {
        MessageBoxWarning("qWDTT", message);
        return false;
    };
    if (get_edit_text_serverAddress().trimmed().isEmpty())
        return invalid(tr("Server address is required."));
    bool portOK;
    const int p = get_edit_text_serverPort().toInt(&portOK);
    if (!portOK || p < 1 || p > 65535)
        return invalid(tr("Server port must be between 1 and 65535."));
    if (password->text().isEmpty() || password->text().contains('|') || password->text().contains('\n') ||
        password->text().contains('\r'))
        return invalid(tr("Enter a valid connection password."));
    if (hashSource->currentData() != "auto" && (values.isEmpty() || values.size() > 4))
        return invalid(tr("Enter one to four VK call hashes."));
    if (device->text().trimmed().isEmpty() || device->text().contains('|'))
        return invalid(tr("Enter a valid device ID."));
    o->mode = mode->currentData().toString();
    o->password = password->text();
    if (hashSource->currentData() == "manual" && (values != o->hashes || o->hashSource != "manual")) o->manualRelayHashes = true;
    o->hashes = values;
    o->hashSource = hashSource->currentData().toString();
    o->autoHashCount = autoHashCount->value();
    o->rawPort = rawPort->value();
    o->workers = workers->currentData().toInt();
    o->turnTCP = transport->currentIndex() == 1;
    o->vkAnonPath = authorization->currentData().toString();
    o->captchaMode = captcha->currentData().toString();
    o->obfs = obfs->currentData().toString();
    o->goDNS = dns->currentText().trimmed();
    o->deviceID = device->text().trimmed();
    return true;
}

void EditQWDTT::updateWorkers(int requested) {
    QStringList values;
    for (auto *field : hashes)
        values.append(field->text());
    const int hashCount =
        hashSource->currentData() == "auto" ? autoHashCount->value() : Configs::qwdtt::NormalizeHashes(values).size();
    if (requested < 0)
        requested = workers->currentData().toInt();
    const int selected = Configs::qwdtt::NormalizeWorkers(requested, hashCount);
    const QSignalBlocker blocker(workers);
    workers->clear();
    for (int count = 9; count <= Configs::qwdtt::MaxWorkers(hashCount); count += 9)
        workers->addItem(QString::number(count), count);
    workers->setCurrentIndex(workers->findData(selected));
}
