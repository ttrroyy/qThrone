#include "include/ui/profile/edit_csqtt.h"
#include "include/configs/outbounds/csqtt.h"
#include "include/global/Configs.hpp"
#include "include/ui/setting/dialog_vk_relay.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTimer>

EditCSQTT::EditCSQTT(QWidget *parent) : QWidget(parent) {
    form = new QFormLayout(this);
    password = new QLineEdit(this);
    password->setEchoMode(QLineEdit::Normal);
    form->addRow(tr("Password"), password);
    // "Авто" takes the calls of the qWDTT VK binding; CSQTT joins them anonymously, never with a VK login.
    hashSource = new QComboBox(this);
    hashSource->setObjectName("csqttHashSource");
    hashSource->addItem(QStringLiteral("Вручную"), "manual");
    hashSource->addItem(QStringLiteral("Авто"), "auto");
    qobject_cast<QStandardItemModel *>(hashSource->model())->item(1)->setEnabled(Configs::dataManager->settingsRepo->vk_relay_linked);
    hashSource->setToolTip(QStringLiteral("«Авто» берёт звонки из привязки аккаунта VK механизмом qWDTT. CSQTT подключается к ним анонимно и не входит в аккаунт VK."));
    form->addRow(QStringLiteral("Источник хешей"), hashSource);
    autoHashCount = new QSpinBox(this);
    autoHashCount->setRange(1, 4);
    form->addRow(QStringLiteral("Количество хешей"), autoHashCount);
    for (int i = 0; i < 6; ++i) {
        hashes[i] = new QLineEdit(this);
        hashes[i]->setObjectName(QString("csqttHash%1").arg(i + 1));
        hashes[i]->setPlaceholderText(tr("Call hash or VK call link"));
        hashRows[i] = new QWidget(this);
        auto *row = new QHBoxLayout(hashRows[i]);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(hashes[i]);
        hashStatus[i] = new QLabel(hashRows[i]);
        row->addWidget(hashStatus[i]);
        connect(hashes[i], &QLineEdit::textChanged, this, [this, i] { hashStatus[i]->clear(); stopHashCheck(); updateWorkers(); });
        form->addRow(tr("VK hash %1").arg(i + 1), hashRows[i]);
    }
    checkHashes = new QPushButton(QStringLiteral("Проверить хеши"), this);
    checkHashes->setObjectName("csqttCheckHashes");
    form->addRow(checkHashes);
    hashProcess = new QProcess(this);
    hashTimeout = new QTimer(this);
    hashTimeout->setSingleShot(true);
    connect(hashTimeout, &QTimer::timeout, this, [this] { stopHashCheck(); });
    connect(hashProcess, &QProcess::readyReadStandardOutput, this, &EditCSQTT::consumeHashCheckOutput);
    // VK's messages can echo the call link: only the verdicts reach the window.
    connect(hashProcess, &QProcess::readyReadStandardError, this, [this] { hashProcess->readAllStandardError(); });
    connect(hashProcess, &QProcess::finished, this, [this] { finishHashCheck(); });
    connect(hashProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finishHashCheck();
    });
    connect(checkHashes, &QPushButton::clicked, this, [this] {
        if (hashProcess->state() != QProcess::NotRunning) stopHashCheck();
        else startHashCheck();
    });
    workers = new QComboBox(this);
    workers->setObjectName("csqttWorkers");
    connect(hashSource, &QComboBox::currentIndexChanged, this, [this] { updateSource(); });
    connect(autoHashCount, &QSpinBox::valueChanged, this, [this] { updateWorkers(); });
    updateWorkers(18);
    form->addRow(tr("Workers"), workers);
    transport = new QComboBox(this);
    transport->addItems({"UDP", "TCP"});
    transport->setCurrentIndex(0);
    form->addRow(tr("TURN transport"), transport);
    authorization = new QComboBox(this);
    authorization->setObjectName("csqttAuthorization");
    authorization->addItem(tr("VK call"), "vkcalls");
    authorization->addItem(tr("Captcha"), "legacy");
    form->addRow(tr("VK authorization"), authorization);
    captcha = new QComboBox(this);
    captcha->setObjectName("csqttCaptcha");
    captcha->addItem(tr("Automatic, with browser fallback"), "auto");
    captcha->addItem(tr("Browser window"), "wv");
    captcha->addItem(tr("Automatic"), "rjs");
    form->addRow(tr("Captcha handling"), captcha);
    obfs = new QComboBox(this);
    obfs->addItem(tr("Audio"), "audio");
    obfs->addItem(tr("Video"), "video");
    form->addRow(tr("Obfuscation"), obfs);
    device = new QLineEdit(this);
    form->addRow(tr("Device ID"), device);
    device->setToolTip(tr("Generated automatically. Keep it unchanged for passwords bound to a device."));
    updateSource();
    auto *accountTask = new RelayTask(this);
    accountTask->start({{"operation", "vk-info"}}, [this](const QJsonObject &event) {
        if (event["type"] == "result") {
            const bool linked = event["value"].toObject()["linked"].toBool();
            qobject_cast<QStandardItemModel *>(hashSource->model())->item(1)->setEnabled(linked);
            if (!linked) hashSource->setCurrentIndex(0);
        }
    });
}

EditCSQTT::~EditCSQTT() {
    hashProcess->disconnect(this);
    if (hashProcess->state() != QProcess::NotRunning) {
        hashProcess->kill();
        hashProcess->waitForFinished(1000);
    }
}

void EditCSQTT::updateSource() {
    const bool automatic = hashSource->currentData() == "auto";
    stopHashCheck();
    for (int i = 0; i < 6; ++i) {
        hashRows[i]->setVisible(!automatic);
        form->labelForField(hashRows[i])->setVisible(!automatic);
    }
    checkHashes->setVisible(!automatic);
    autoHashCount->setVisible(automatic);
    form->labelForField(autoHashCount)->setVisible(automatic);
    updateWorkers();
}

// CSQTT's own check, as its original app runs it: --validate-vk-hashes asks VK for an anonymous
// token of each call, one after another, without TURN credentials or a captcha.
void EditCSQTT::startHashCheck() {
    checking.clear();
    for (int i = 0; i < 6; ++i) {
        hashStatus[i]->clear();
        const auto normalized = Configs::csqtt::NormalizeHashes({hashes[i]->text()});
        if (!normalized.isEmpty() && QRegularExpression("^[A-Za-z0-9_-]{16,512}$").match(normalized.first()).hasMatch())
            checking.append({i, normalized.first()});
        else if (!normalized.isEmpty()) hashStatus[i]->setText(QStringLiteral("Некорректный хеш"));
    }
    if (checking.isEmpty()) { MessageBoxWarning("CSQTT", QStringLiteral("Укажите хеши для проверки.")); return; }
    QString executable = "csqtt-transport";
#ifdef Q_OS_WIN
    executable += ".exe";
#endif
    executable = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(executable);
    if (!QFileInfo(executable).isExecutable()) { MessageBoxWarning("CSQTT", QStringLiteral("Клиент CSQTT не найден.")); return; }
    // The hashes travel in a private file, not on the command line other users can read.
    hashCheckDirectory = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/qthrone-csqtt-check-XXXXXX");
    QFile config(hashCheckDirectory->filePath("check.json"));
    if (!hashCheckDirectory->isValid() || !config.open(QIODevice::WriteOnly)) {
        MessageBoxWarning("CSQTT", QStringLiteral("Не удалось создать файл проверки.")); hashCheckDirectory.reset(); return;
    }
    config.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    QJsonArray values;
    for (const auto &[slot, hash] : checking) values.append(hash);
    const auto data = QJsonDocument(QJsonObject{{"hashes", values}}).toJson(QJsonDocument::Compact);
    const bool written = config.write(data) == data.size();
    config.close();
    if (!written) { hashCheckDirectory.reset(); return; }
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("CSQTT_CONFIG_FILE", config.fileName());
    hashProcess->setProcessEnvironment(environment);
    hashProcess->setWorkingDirectory(hashCheckDirectory->path());
    hashCheckStopped = false;
    hashCheckOutput.clear();
    for (const auto &[slot, hash] : checking) hashStatus[slot]->setText(QStringLiteral("Проверяется…"));
    checkHashes->setText(QStringLiteral("СТОП"));
    hashProcess->start(executable, {"--validate-vk-hashes", "--client-ids", "8202606,6287487"});
    // The original allows 75 s for the whole list.
    hashTimeout->start(75000);
}

void EditCSQTT::consumeHashCheckOutput() {
    hashCheckOutput += hashProcess->readAllStandardOutput();
    while (hashCheckOutput.contains('\n')) {
        const auto end = hashCheckOutput.indexOf('\n');
        const auto line = QString::fromUtf8(hashCheckOutput.left(end)).trimmed();
        hashCheckOutput.remove(0, end + 1);
        const auto at = line.indexOf("HASH_CHECK:");
        if (hashCheckStopped || at < 0) continue;
        const auto result = QJsonDocument::fromJson(line.mid(at + 11).toUtf8()).object();
        const auto status = result["status"].toString();
        const QString text = status == "valid" ? QStringLiteral("Звонок доступен")
                           : status == "invalid" ? QStringLiteral("Звонок закрыт или хеш недействителен")
                           : QStringLiteral("Не удалось проверить");
        for (const auto &[slot, hash] : checking)
            if (hash == result["hash"].toString()) hashStatus[slot]->setText(text);
    }
}

void EditCSQTT::stopHashCheck() {
    if (!hashProcess || hashProcess->state() == QProcess::NotRunning || hashCheckStopped) return;
    hashCheckStopped = true;
    hashProcess->kill();
}

void EditCSQTT::finishHashCheck() {
    consumeHashCheckOutput();
    hashTimeout->stop();
    checkHashes->setText(QStringLiteral("Проверить хеши"));
    for (const auto &[slot, hash] : checking) {
        if (hashStatus[slot]->text() == QStringLiteral("Проверяется…"))
            hashStatus[slot]->setText(hashCheckStopped ? QStringLiteral("Остановлено") : QStringLiteral("Нет ответа проверки"));
    }
    hashCheckDirectory.reset();
}

void EditCSQTT::onStart(std::shared_ptr<Configs::Profile> profile) {
    ent = std::move(profile);
    auto *o = dynamic_cast<Configs::csqtt *>(ent->outbound.get());
    if (!o) return;
    password->setText(o->password);
    autoHashCount->setValue(o->autoHashCount);
    hashSource->setCurrentIndex(o->hashSource == "auto" ? 1 : 0);
    for (int i = 0; i < 6; ++i) hashes[i]->setText(i < o->hashes.size() ? o->hashes[i] : QString());
    updateSource();
    updateWorkers(o->workers);
    transport->setCurrentIndex(o->turnTCP ? 1 : 0);
    authorization->setCurrentIndex(authorization->findData(o->vkAnonPath));
    captcha->setCurrentIndex(captcha->findData(o->captchaMode));
    obfs->setCurrentIndex(o->obfs == "video" ? 1 : 0);
    device->setText(o->deviceID);
}

bool EditCSQTT::onEnd() {
    auto *o = dynamic_cast<Configs::csqtt *>(ent->outbound.get());
    if (!o) return false;
    QStringList values;
    for (auto *field : hashes) values.append(field->text());
    values = Configs::csqtt::NormalizeHashes(values);
    const bool automatic = hashSource->currentData() == "auto";
    const auto invalid = [this](const QString &message){ MessageBoxWarning("CSQTT", message); return false; };
    if (get_edit_text_serverAddress().trimmed().isEmpty()) return invalid(tr("Server address is required."));
    bool portOK;
    const int p = get_edit_text_serverPort().toInt(&portOK);
    if (!portOK || p < 1 || p > 65535) return invalid(tr("Server port must be between 1 and 65535."));
    if (password->text().isEmpty() || password->text().contains('|') || password->text().contains('\n') || password->text().contains('\r')) return invalid(tr("Enter a valid connection password."));
    if (!automatic && (values.isEmpty() || values.size() > 6)) return invalid(tr("Enter one to six VK call hashes."));
    QSet<QString> seen;
    for (const auto &hash : values) {
        if (hash.size() < 16 || hash.contains(QRegularExpression("[,|\\s]")) || seen.contains(hash)) return invalid(tr("Enter valid, distinct VK call hashes."));
        seen.insert(hash);
    }
    if (device->text().trimmed().isEmpty() || device->text().contains('|')) return invalid(tr("Enter a valid device ID."));
    o->password = password->text();
    o->hashSource = hashSource->currentData().toString();
    o->autoHashCount = autoHashCount->value();
    o->hashes = values;
    o->workers = workers->currentData().toInt();
    o->turnTCP = transport->currentIndex() == 1;
    o->vkAnonPath = authorization->currentData().toString();
    o->captchaMode = captcha->currentData().toString();
    o->obfs = obfs->currentData().toString();
    o->deviceID = device->text().trimmed();
    return true;
}

void EditCSQTT::updateWorkers(int requested) {
    QStringList values;
    for (auto *field : hashes) values.append(field->text());
    const int hashCount = hashSource->currentData() == "auto" ? autoHashCount->value() : Configs::csqtt::NormalizeHashes(values).size();
    if (requested < 0) requested = workers->currentData().toInt();
    const int selected = Configs::csqtt::NormalizeWorkers(requested, hashCount);
    const QSignalBlocker blocker(workers);
    workers->clear();
    for (int count = 9; count <= Configs::csqtt::MaxWorkers(hashCount); count += 9)
        workers->addItem(QString::number(count), count);
    workers->setCurrentIndex(workers->findData(selected));
}
