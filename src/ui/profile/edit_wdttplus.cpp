#include "include/ui/profile/edit_wdttplus.h"
#include "include/configs/outbounds/qwdtt.h"
#include "include/configs/outbounds/wdttplus.h"
#include "include/global/Configs.hpp"
#include "include/ui/setting/dialog_vk_relay.h"
#include <QCoreApplication>
#include <QCheckBox>
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
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTimer>

EditWdttPlus::EditWdttPlus(QWidget *parent) : QWidget(parent) {
    form = new QFormLayout(this);
    password = new QLineEdit(this);
    form->addRow(tr("Password"), password);
    engine = new QComboBox(this);
    engine->addItem(QStringLiteral("WDTT Plus"), "wdttplus"); engine->addItem(QStringLiteral("qWDTT"), "qwdtt");
    engine->setToolTip(QStringLiteral("Клиент, с которым работает сервер. По ссылке WDTT этого не узнать: если подключение не идёт, выберите другой."));
    form->addRow(QStringLiteral("Движок"), engine);
    qwdttMode = new QComboBox(this);
    qwdttMode->addItem("WG", "wg"); qwdttMode->addItem("RAW", "raw");
    form->addRow(QStringLiteral("Режим qWDTT"), qwdttMode);
    rawPort = new QSpinBox(this);
    rawPort->setRange(1, 65535);
    form->addRow(QStringLiteral("Порт RAW"), rawPort);
    connect(engine, &QComboBox::currentIndexChanged, this, [this] { updateEngine(); });
    connect(qwdttMode, &QComboBox::currentIndexChanged, this, [this] { updateEngine(); });
    // "Авто" takes the calls of the qWDTT VK binding; WDTT Plus never logs into VK itself.
    hashSource = new QComboBox(this);
    hashSource->setObjectName("wdttPlusHashSource");
    hashSource->addItem(QStringLiteral("Вручную"), "manual");
    hashSource->addItem(QStringLiteral("Созданные в клиенте"), "auto");
    qobject_cast<QStandardItemModel *>(hashSource->model())->item(1)->setEnabled(Configs::dataManager->settingsRepo->vk_relay_linked);
    hashSource->setToolTip(QStringLiteral("«Авто» берёт звонки из привязки аккаунта VK механизмом qWDTT. WDTT Plus подключается к ним и не входит в аккаунт VK."));
    form->addRow(QStringLiteral("Источник хешей"), hashSource);
    autoHashCount = new QSpinBox(this);
    autoHashCount->setRange(1, 4);
    form->addRow(QStringLiteral("Количество хешей"), autoHashCount);
    for (int i = 0; i < 4; ++i) {
        hashes[i] = new QLineEdit(this);
        hashes[i]->setObjectName(QString("wdttPlusHash%1").arg(i + 1));
        hashes[i]->setPlaceholderText(tr("Call hash or VK call link"));
        hashRows[i] = new QWidget(this);
        auto *row = new QHBoxLayout(hashRows[i]);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(hashes[i]);
        hashStatus[i] = new QLabel(hashRows[i]);
        row->addWidget(hashStatus[i]);
        connect(hashes[i], &QLineEdit::textChanged, this, [this, i] { hashStatus[i]->clear(); stopHashCheck(); });
        form->addRow(tr("VK hash %1").arg(i + 1), hashRows[i]);
    }
    checkHashes = new QPushButton(QStringLiteral("Проверить хеши"), this);
    checkHashes->setObjectName("wdttPlusCheckHashes");
    form->addRow(checkHashes);
    hashProcess = new QProcess(this);
    hashTimeout = new QTimer(this);
    hashTimeout->setSingleShot(true);
    connect(hashTimeout, &QTimer::timeout, this, [this] { stopHashCheck(); });
    connect(hashProcess, &QProcess::readyReadStandardOutput, this, &EditWdttPlus::consumeHashCheckOutput);
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
    workers->setObjectName("wdttPlusWorkers");
    connect(hashSource, &QComboBox::currentIndexChanged, this, [this] { updateSource(); });
    updateWorkers(18);
    form->addRow(tr("Workers"), workers);
    captcha = new QComboBox(this);
    captcha->setObjectName("wdttPlusCaptcha");
    captcha->addItem(tr("Automatic, with browser fallback"), "auto");
    captcha->addItem(tr("Browser window"), "wv");
    captcha->addItem(tr("Automatic"), "rjs");
    form->addRow(tr("Captcha handling"), captcha);
    tcpOnly = new QCheckBox(QStringLiteral("Только TURN/TLS, TCP и MASQUE HTTP/2"), this); form->addRow(tcpOnly);
    turnSni = new QLineEdit(this); form->addRow(QStringLiteral("Внешний SNI TURN/TLS"), turnSni);
    rtMasque = new QCheckBox(QStringLiteral("Резерв WARP MASQUE (принимаю условия Cloudflare)"), this); form->addRow(rtMasque);
    serverBootstrap = new QCheckBox(QStringLiteral("Первая регистрация MASQUE через сохранённый VPS"), this); form->addRow(serverBootstrap);
    preflight = new QCheckBox(QStringLiteral("Предварительная проверка VK Calls"), this); form->addRow(preflight);
    fingerprint = new QComboBox(this); fingerprint->addItems({"firefox","chrome","safari","ios","android","random"});
    form->addRow(QStringLiteral("TLS-отпечаток"), fingerprint);
    clientIds = new QLineEdit(this); form->addRow(QStringLiteral("ID клиентов VK"), clientIds);
    wgPort = new QSpinBox(this);
    wgPort->setRange(1, 65535);
    form->addRow(QStringLiteral("Порт WG"), wgPort);
    localPort = new QSpinBox(this);
    localPort->setRange(1, 65535);
    form->addRow(QStringLiteral("Локальный порт сервера"), localPort);
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

EditWdttPlus::~EditWdttPlus() {
    hashProcess->disconnect(this);
    if (hashProcess->state() != QProcess::NotRunning) {
        hashProcess->kill();
        hashProcess->waitForFinished(1000);
    }
}

QString EditWdttPlus::HashVerdict(const QString &status) {
    // Provider errors can contain credentials: only the client's verdict is shown.
    static const QHash<QString, QString> texts{
        {"ok", QStringLiteral("TURN доступен")}, {"dead", QStringLiteral("Звонок закрыт или хеш недействителен")},
        {"captcha", QStringLiteral("Нужна капча VK")}, {"limited", QStringLiteral("VK ограничил частоту запросов")},
        {"network", QStringLiteral("Не достучались по сети/DNS")}, {"full", QStringLiteral("Звонок переполнен")},
        {"blocked", QStringLiteral("VK закрыл доступ к звонку")}, {"auth_required", QStringLiteral("Нужен вход в VK")}};
    return texts.value(status, QStringLiteral("Диагностика не удалась"));
}

void EditWdttPlus::updateSource() {
    const bool automatic = hashSource->currentData() == "auto";
    stopHashCheck();
    for (int i = 0; i < 4; ++i) {
        hashRows[i]->setVisible(!automatic);
        form->labelForField(hashRows[i])->setVisible(!automatic);
    }
    checkHashes->setVisible(!automatic);
    autoHashCount->setVisible(automatic);
    form->labelForField(autoHashCount)->setVisible(automatic);
}

// WDTT Plus's own check, as its app runs it: its client with -check-hashes, its captcha
// chain answered by qThrone's browser window, 90 s per hash.
void EditWdttPlus::startHashCheck() {
    checking.clear();
    QJsonArray values;
    const QRegularExpression valid("^[A-Za-z0-9_-]{16,512}$");
    for (int i = 0; i < 4; ++i) {
        hashStatus[i]->clear();
        const auto normalized = Configs::qwdtt::NormalizeHashes({hashes[i]->text()});
        if (!normalized.isEmpty() && valid.match(normalized.first()).hasMatch() && !values.contains(normalized.first())) {
            checking.append(i);
            values.append(normalized.first());
        } else if (!normalized.isEmpty()) hashStatus[i]->setText(QStringLiteral("Некорректный хеш"));
    }
    if (checking.isEmpty()) { MessageBoxWarning("WDTT Plus", QStringLiteral("Укажите хеши для проверки.")); return; }
    QString helper = "qwdtt", client = "wdttplus-client";
#ifdef Q_OS_WIN
    helper += ".exe"; client += ".exe";
#endif
    const QDir directory(QCoreApplication::applicationDirPath());
    if (!QFileInfo(directory.absoluteFilePath(helper)).isExecutable() || !QFileInfo(directory.absoluteFilePath(client)).isExecutable()) {
        MessageBoxWarning("WDTT Plus", QStringLiteral("Клиент WDTT Plus не найден.")); return;
    }
    // The hashes travel in a private file, not on the command line other users can read.
    hashCheckDirectory = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/qthrone-wdttplus-check-XXXXXX");
    QFile config(hashCheckDirectory->filePath("check.json"));
    if (!hashCheckDirectory->isValid() || !config.open(QIODevice::WriteOnly)) {
        MessageBoxWarning("WDTT Plus", QStringLiteral("Не удалось создать файл проверки.")); hashCheckDirectory.reset(); return;
    }
    config.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const auto data = QJsonDocument(QJsonObject{{"hashes", values}, {"captcha_mode", captcha->currentData().toString()}}).toJson(QJsonDocument::Compact);
    const bool written = config.write(data) == data.size();
    config.close();
    if (!written) { hashCheckDirectory.reset(); return; }
    hashCheckStopped = false;
    hashCheckOutput.clear();
    for (const int slot : checking) hashStatus[slot]->setText(QStringLiteral("Ожидает…"));
    checkHashes->setText(QStringLiteral("СТОП"));
    hashProcess->start(directory.absoluteFilePath(helper), {"-wdttplus-check", config.fileName()});
    // 90 s per hash in the client, and a little for its start.
    hashTimeout->start(int(checking.size()) * 100000);
}

void EditWdttPlus::consumeHashCheckOutput() {
    hashCheckOutput += hashProcess->readAllStandardOutput();
    while (hashCheckOutput.contains('\n')) {
        const auto end = hashCheckOutput.indexOf('\n');
        const auto parts = QString::fromUtf8(hashCheckOutput.left(end)).trimmed().split('|');
        hashCheckOutput.remove(0, end + 1);
        if (hashCheckStopped || parts.size() < 2) continue;
        const int index = parts[1].toInt() - 1;
        if (index < 0 || index >= checking.size()) continue;
        if (parts[0] == "HASH_CHECK_START") hashStatus[checking[index]]->setText(QStringLiteral("Проверяется…"));
        else if (parts[0] == "HASH_CHECK" && parts.size() == 3) hashStatus[checking[index]]->setText(HashVerdict(parts[2]));
    }
}

void EditWdttPlus::stopHashCheck() {
    if (!hashProcess || hashProcess->state() == QProcess::NotRunning || hashCheckStopped) return;
    hashCheckStopped = true;
    hashProcess->kill();
}

void EditWdttPlus::finishHashCheck() {
    consumeHashCheckOutput();
    hashTimeout->stop();
    checkHashes->setText(QStringLiteral("Проверить хеши"));
    for (const int slot : checking) {
        const auto text = hashStatus[slot]->text();
        if (text == QStringLiteral("Проверяется…") || text == QStringLiteral("Ожидает…"))
            hashStatus[slot]->setText(hashCheckStopped ? QStringLiteral("Остановлено") : QStringLiteral("Нет ответа проверки"));
    }
    hashCheckDirectory.reset();
}

void EditWdttPlus::onStart(std::shared_ptr<Configs::Profile> profile) {
    ent = std::move(profile);
    auto *o = dynamic_cast<Configs::wdttplus *>(ent->outbound.get());
    if (!o) return;
    password->setText(o->password);
    autoHashCount->setValue(o->autoHashCount);
    hashSource->setCurrentIndex(o->hashSource == "auto" ? 1 : 0);
    for (int i = 0; i < 4; ++i) hashes[i]->setText(i < o->hashes.size() ? o->hashes[i] : QString());
    updateSource();
    maxWorkers = o->maxWorkers;
    updateWorkers(o->workers);
    captcha->setCurrentIndex(qMax(0, captcha->findData(o->captchaMode)));
    tcpOnly->setChecked(o->tcpOnly); turnSni->setText(o->turnSni);
    rtMasque->setChecked(o->rtMasque); serverBootstrap->setChecked(o->rtMasqueServerBootstrap);
    preflight->setChecked(o->vkCallsPreflight); fingerprint->setCurrentText(o->fingerprint); clientIds->setText(o->clientIds);
    wgPort->setValue(o->wgPort);
    localPort->setValue(o->localPort);
    device->setText(o->deviceID);
    engine->setCurrentIndex(qMax(0, engine->findData(o->engine)));
    qwdttMode->setCurrentIndex(qMax(0, qwdttMode->findData(o->mode)));
    rawPort->setValue(o->rawPort);
    // Only a plain WDTT link leaves the client open; a wdtt://connect link is WDTT Plus's own.
    for (auto *field : std::initializer_list<QWidget *>{engine, qwdttMode, rawPort}) field->setProperty("wdttLegacy", o->legacy);
    updateEngine();
}

void EditWdttPlus::updateEngine() {
    const bool legacy = engine->property("wdttLegacy").toBool();
    const bool qwdtt = legacy && engine->currentData() == "qwdtt";
    engine->setVisible(legacy); form->labelForField(engine)->setVisible(legacy);
    qwdttMode->setVisible(qwdtt); form->labelForField(qwdttMode)->setVisible(qwdtt);
    const bool raw = qwdtt && qwdttMode->currentData() == "raw";
    rawPort->setVisible(raw); form->labelForField(rawPort)->setVisible(raw);
}

bool EditWdttPlus::onEnd() {
    auto *o = dynamic_cast<Configs::wdttplus *>(ent->outbound.get());
    if (!o) return false;
    QStringList values;
    for (auto *field : hashes) values.append(field->text());
    values = Configs::qwdtt::NormalizeHashes(values);
    const bool automatic = hashSource->currentData() == "auto";
    const auto invalid = [](const QString &message) { MessageBoxWarning("WDTT Plus", message); return false; };
    if (get_edit_text_serverAddress().trimmed().isEmpty()) return invalid(tr("Server address is required."));
    bool portOK;
    const int p = get_edit_text_serverPort().toInt(&portOK);
    if (!portOK || p < 1 || p > 65535) return invalid(tr("Server port must be between 1 and 65535."));
    if (password->text().isEmpty() || password->text().contains('\n') || password->text().contains('\r')) return invalid(tr("Enter a valid connection password."));
    if (!automatic && values.size() > 4) return invalid(QStringLiteral("Укажите не больше четырёх хешей звонков VK."));
    const QRegularExpression valid("^[A-Za-z0-9_-]{16,512}$");
    for (const auto &hash : values)
        if (!valid.match(hash).hasMatch()) return invalid(tr("Enter valid, distinct VK call hashes."));
    if (device->text().trimmed().isEmpty() || device->text().contains('\n')) return invalid(tr("Enter a valid device ID."));
    if (!Configs::wdttplus::ValidTurnSni(turnSni->text().trimmed().toLower())) return invalid(QStringLiteral("Введите домен для SNI TURN/TLS."));
    if (!QRegularExpression("^[0-9]+(?:,[0-9]+){0,15}$").match(clientIds->text()).hasMatch()) return invalid(QStringLiteral("ID клиентов VK — числа через запятую."));
    o->password = password->text();
    const auto previousHashSource = o->hashSource;
    o->hashSource = hashSource->currentData().toString();
    o->autoHashCount = autoHashCount->value();
    if (!automatic && (values != o->hashes || previousHashSource != "manual")) o->manualRelayHashes = true;
    o->hashes = values;
    o->workers = workers->currentData().toInt();
    o->captchaMode = captcha->currentData().toString();
    o->tcpOnly = tcpOnly->isChecked(); o->turnSni = turnSni->text().trimmed().toLower();
    o->rtMasque = rtMasque->isChecked(); o->rtMasqueServerBootstrap = serverBootstrap->isChecked();
    o->vkCallsPreflight = preflight->isChecked(); o->fingerprint = fingerprint->currentText(); o->clientIds = clientIds->text();
    o->wgPort = wgPort->value();
    o->localPort = localPort->value();
    o->deviceID = device->text().trimmed();
    if (o->legacy) {
        o->engine = engine->currentData().toString();
        o->mode = qwdttMode->currentData().toString();
        o->rawPort = rawPort->value();
    }
    return true;
}

void EditWdttPlus::updateWorkers(int requested) {
    if (requested < 0) requested = workers->currentData().toInt();
    const int selected = Configs::wdttplus::NormalizeWorkers(requested, maxWorkers);
    const QSignalBlocker blocker(workers);
    workers->clear();
    for (int count = 9; count <= Configs::wdttplus::WorkerLimit(maxWorkers); count += 9)
        workers->addItem(QString::number(count), count);
    workers->setCurrentIndex(workers->findData(selected));
}
