#include "include/ui/profile/edit_mrtt.h"
#include "include/configs/outbounds/mrtt.h"
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

EditMRTT::EditMRTT(QWidget *parent) : QWidget(parent) {
    form = new QFormLayout(this);
    password = new QLineEdit(this);
    password->setEchoMode(QLineEdit::Normal);
    form->addRow(tr("Password"), password);
    mode = new QComboBox(this);
    mode->setObjectName("mrttMode");
    mode->addItem("VK", "vk"); mode->addItem("MAX", "max"); mode->addItem("Jitsi", "jitsi");
    form->addRow(QStringLiteral("Транспорт"), mode);
    maxGroup = new QLineEdit(this);
    maxGroup->setObjectName("mrttMaxGroup");
    maxGroup->setPlaceholderText(QStringLiteral("Пусто — группа привязки MAX"));
    form->addRow(QStringLiteral("ID группы MAX"), maxGroup);
    jitsiRoom = new QLineEdit(this);
    jitsiRoom->setObjectName("mrttJitsiRoom");
    jitsiRoom->setPlaceholderText("https://meet.example.org/room");
    form->addRow(QStringLiteral("Ссылка на комнату Jitsi"), jitsiRoom);
    jitsiWorkers = new QSpinBox(this);
    jitsiWorkers->setRange(Configs::mrtt::MinJitsiWorkers, Configs::mrtt::MaxJitsiWorkers);
    form->addRow(QStringLiteral("Воркеры Jitsi"), jitsiWorkers);
    // Every account mode uses the client's one VK binding; a link may supply its own Auto JS token.
    hashSource = new QComboBox(this);
    hashSource->setObjectName("mrttHashSource");
    hashSource->addItem(QStringLiteral("Вручную"), "manual");
    hashSource->addItem(QStringLiteral("Созданные в клиенте"), "auto");
    hashSource->addItem(QStringLiteral("Авто JS"), "auto_js");
    qobject_cast<QStandardItemModel *>(hashSource->model())->item(1)->setEnabled(Configs::dataManager->settingsRepo->vk_relay_linked);
    hashSource->setToolTip(QStringLiteral("«Созданные в клиенте» берёт хеши, созданные в этом клиенте через привязку VK, и при нехватке создаёт новые. «Авто JS» использует токен этой же привязки или токен из ссылки."));
    form->addRow(QStringLiteral("Источник хешей"), hashSource);
    autoHashCount = new QSpinBox(this);
    autoHashCount->setRange(1, 6);
    form->addRow(QStringLiteral("Количество хешей"), autoHashCount);
    auto *pickHashes = new QPushButton(QStringLiteral("Выбрать хеши из созданных"), this);
    pickHashes->setObjectName("relayPickHashes");
    pickHashes->setToolTip(QStringLiteral("Какие из созданных хешей использовать для этого сервера. Если ничего не выбрано, работает количество выше."));
    form->addRow(pickHashes);
    connect(pickHashes, &QPushButton::clicked, this, [this] {
        RelayPickHashes(this, "mrtt", ent && ent->outbound ? ent->outbound->server : QString(), ent && ent->outbound ? ent->outbound->server_port : 0, 6);
    });
    token = new QLineEdit(this);
    token->setEchoMode(QLineEdit::Password);
    token->setPlaceholderText(QStringLiteral("Пусто — токен привязанного аккаунта VK"));
    form->addRow(QStringLiteral("Токен VK для Auto JS"), token);
    for (int i = 0; i < 6; ++i) {
        hashes[i] = new QLineEdit(this);
        hashes[i]->setObjectName(QString("mrttHash%1").arg(i + 1));
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
    checkHashes->setObjectName("mrttCheckHashes");
    form->addRow(checkHashes);
    hashProcess = new QProcess(this);
    hashTimeout = new QTimer(this);
    hashTimeout->setSingleShot(true);
    connect(hashTimeout, &QTimer::timeout, this, [this] { stopHashCheck(); });
    connect(hashProcess, &QProcess::readyReadStandardOutput, this, &EditMRTT::consumeHashCheckOutput);
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
    workers->setObjectName("mrttWorkers");
    connect(hashSource, &QComboBox::currentIndexChanged, this, [this] { updateSource(); });
    connect(autoHashCount, &QSpinBox::valueChanged, this, [this] { updateWorkers(); });
    updateWorkers(18);
    form->addRow(tr("Workers"), workers);
    transport = new QComboBox(this);
    transport->addItem("UDP", "udp");
    transport->addItem("TCP", "tcp_tls");
    transport->setCurrentIndex(0);
    form->addRow(tr("TURN transport"), transport);
    authorization = new QComboBox(this);
    authorization->setObjectName("mrttAuthorization");
    authorization->addItem(tr("VK call"), "vkcalls");
    authorization->addItem(tr("Captcha"), "legacy");
    form->addRow(tr("VK authorization"), authorization);
    captcha = new QComboBox(this);
    captcha->setObjectName("mrttCaptcha");
    captcha->addItem(tr("Automatic, with browser fallback"), "auto");
    captcha->addItem(tr("Browser window"), "wv");
    captcha->addItem(tr("Automatic"), "rjs");
    form->addRow(tr("Captcha handling"), captcha);
    srtp = new QComboBox(this);
    srtp->addItem(QStringLiteral("Авто"), "auto"); srtp->addItem("WRAP-SRTP", "wrap"); srtp->addItem("DTLS-SRTP", "dtls");
    form->addRow("SRTP", srtp);
    fingerprint = new QComboBox(this);
    fingerprint->addItems({"firefox", "chrome", "safari", "ios", "android", "random"});
    form->addRow(QStringLiteral("TLS-отпечаток"), fingerprint);
    clientIds = new QLineEdit(this); form->addRow(QStringLiteral("ID клиентов VK"), clientIds);
    customTurnIps = new QLineEdit(this); form->addRow(QStringLiteral("Собственные IP TURN"), customTurnIps);
    mtu = new QComboBox(this);
    mtu->addItem("1280", 1280); mtu->addItem("1400", 1400); mtu->addItem("1500", 1500);
    form->addRow("MTU", mtu);
    connect(mode, &QComboBox::activated, this, [this] {
        if (mode->currentData() == "max" && !ConfirmMaxRisk(this)) mode->setCurrentIndex(0);
    });
    connect(mode, &QComboBox::currentIndexChanged, this, [this] { updateMode(); });
    updateMode();
    updateSource();
    auto *accountTask = new RelayTask(this);
    accountTask->start({{"operation", "vk-info"}}, [this](const QJsonObject &event) {
        if (event["type"] == "result") {
            const bool linked = event["value"].toObject()["linked"].toBool();
            qobject_cast<QStandardItemModel *>(hashSource->model())->item(1)->setEnabled(linked);
            if (!linked && hashSource->currentData() == "auto") hashSource->setCurrentIndex(0);
        }
    });
}

EditMRTT::~EditMRTT() {
    hashProcess->disconnect(this);
    if (hashProcess->state() != QProcess::NotRunning) {
        hashProcess->kill();
        hashProcess->waitForFinished(1000);
    }
}

void EditMRTT::showVk(QWidget *field, bool visible) {
    field->setVisible(visible);
    if (auto *label = form->labelForField(field)) label->setVisible(visible);
}

// The settings of each transport, as the original shows only the ones of the chosen one.
void EditMRTT::updateMode() {
    const auto chosen = mode->currentData().toString();
    showVk(maxGroup, chosen == "max");
    showVk(jitsiRoom, chosen == "jitsi");
    showVk(jitsiWorkers, chosen == "jitsi");
    // UDP or TCP picks the way to TURN; Jitsi goes through the room without TURN.
    showVk(transport, chosen != "jitsi");
    showVk(customTurnIps, chosen != "jitsi");
    for (auto *field : {static_cast<QWidget *>(hashSource), static_cast<QWidget *>(authorization), static_cast<QWidget *>(captcha), static_cast<QWidget *>(clientIds)}) showVk(field, chosen == "vk");
    showVk(workers, chosen != "jitsi");
    updateSource();
}

void EditMRTT::updateSource() {
    const bool vk = mode->currentData() == "vk";
    const bool automatic = !vk || hashSource->currentData() != "manual";
    stopHashCheck();
    for (int i = 0; i < 6; ++i) {
        hashRows[i]->setVisible(!automatic);
        form->labelForField(hashRows[i])->setVisible(!automatic);
    }
    checkHashes->setVisible(!automatic);
    const bool api = vk && hashSource->currentData() == "auto";
    autoHashCount->setVisible(api);
    if (auto *pick = findChild<QPushButton *>("relayPickHashes")) pick->setVisible(api);
    form->labelForField(autoHashCount)->setVisible(api);
    token->setVisible(vk && hashSource->currentData() == "auto_js");
    form->labelForField(token)->setVisible(vk && hashSource->currentData() == "auto_js");
    authorization->setEnabled(hashSource->currentData() != "auto_js");
    updateWorkers();
}

// MRTT's own check, as its original app runs it: --validate-vk-hashes asks VK for an anonymous
// token of each call, one after another, without TURN credentials or a captcha.
void EditMRTT::startHashCheck() {
    checking.clear();
    for (int i = 0; i < 6; ++i) {
        hashStatus[i]->clear();
        const auto normalized = Configs::mrtt::NormalizeHashes({hashes[i]->text()});
        if (!normalized.isEmpty() && QRegularExpression("^[A-Za-z0-9_-]{16,512}$").match(normalized.first()).hasMatch())
            checking.append({i, normalized.first()});
        else if (!normalized.isEmpty()) hashStatus[i]->setText(QStringLiteral("Некорректный хеш"));
    }
    if (checking.isEmpty()) { MessageBoxWarning("MRTT", QStringLiteral("Укажите хеши для проверки.")); return; }
    QString executable = "mrtt-transport";
#ifdef Q_OS_WIN
    executable += ".exe";
#endif
    executable = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(executable);
    if (!QFileInfo(executable).isExecutable()) { MessageBoxWarning("MRTT", QStringLiteral("Клиент MRTT не найден.")); return; }
    hashCheckStopped = false;
    hashCheckOutput.clear();
    for (const auto &[slot, hash] : checking) hashStatus[slot]->setText(QStringLiteral("Проверяется…"));
    checkHashes->setText(QStringLiteral("СТОП"));
    QStringList validated;
    for (const auto &[slot, hash] : checking) validated << hash;
    // The original client's own arguments: it reads the list from --vk.
    hashProcess->start(executable, {"--validate-vk-hashes", "--vk", validated.join(','), "--client-ids", "8202606,6287487"});
    // The original allows 75 s for the whole list.
    hashTimeout->start(75000);
}

void EditMRTT::consumeHashCheckOutput() {
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

void EditMRTT::stopHashCheck() {
    if (!hashProcess || hashProcess->state() == QProcess::NotRunning || hashCheckStopped) return;
    hashCheckStopped = true;
    hashProcess->kill();
}

void EditMRTT::finishHashCheck() {
    consumeHashCheckOutput();
    hashTimeout->stop();
    checkHashes->setText(QStringLiteral("Проверить хеши"));
    for (const auto &[slot, hash] : checking) {
        if (hashStatus[slot]->text() == QStringLiteral("Проверяется…"))
            hashStatus[slot]->setText(hashCheckStopped ? QStringLiteral("Остановлено") : QStringLiteral("Нет ответа проверки"));
    }
}

void EditMRTT::onStart(std::shared_ptr<Configs::Profile> profile) {
    ent = std::move(profile);
    auto *o = dynamic_cast<Configs::mrtt *>(ent->outbound.get());
    if (!o) return;
    password->setText(o->password);
    mode->setCurrentIndex(qMax(0, mode->findData(o->transportMode)));
    maxGroup->setText(o->maxGroup);
    jitsiRoom->setText(o->jitsiRoom.isEmpty() ? QString() : "https://" + o->jitsiRoom);
    jitsiWorkers->setValue(o->jitsiWorkers);
    mtu->setCurrentIndex(qMax(0, mtu->findData(o->mtu)));
    autoHashCount->setValue(o->autoHashCount);
    hashSource->setCurrentIndex(hashSource->findData(o->hashSource));
    token->setText(o->vkAccessToken);
    for (int i = 0; i < 6; ++i) hashes[i]->setText(i < o->hashes.size() ? o->hashes[i] : QString());
    updateSource();
    updateMode();
    updateWorkers(o->workers);
    transport->setCurrentIndex(qMax(0, transport->findData(o->turnTransport)));
    authorization->setCurrentIndex(qMax(0, authorization->findData(o->vkAnonPath)));
    captcha->setCurrentIndex(captcha->findData(o->captchaMode));
    srtp->setCurrentIndex(srtp->findData(o->srtpMode));
    fingerprint->setCurrentText(o->fingerprint);
    clientIds->setText(o->clientIds); customTurnIps->setText(o->customTurnIps);
}

bool EditMRTT::onEnd() {
    auto *o = dynamic_cast<Configs::mrtt *>(ent->outbound.get());
    if (!o) return false;
    QStringList values;
    for (auto *field : hashes) values.append(field->text());
    values = Configs::mrtt::NormalizeHashes(values);
    const bool automatic = hashSource->currentData() != "manual";
    const auto invalid = [this](const QString &message){ MessageBoxWarning("MRTT", message); return false; };
    if (get_edit_text_serverAddress().trimmed().isEmpty()) return invalid(tr("Server address is required."));
    bool portOK;
    const int p = get_edit_text_serverPort().toInt(&portOK);
    if (!portOK || p < 1 || p > 65535) return invalid(tr("Server port must be between 1 and 65535."));
    if (!Configs::mrtt::ValidPassword(password->text())) return invalid(QStringLiteral("Пароль: без пробелов и «|», не длиннее 128 байт."));
    const auto chosenRoom = jitsiRoom->text().trimmed().isEmpty() ? QString() : Configs::mrtt::ParseJitsiRoom(jitsiRoom->text());
    if (mode->currentData() == "jitsi" && chosenRoom.isEmpty()) return invalid(QStringLiteral("Вставьте ссылку на комнату Jitsi."));
    if (!jitsiRoom->text().trimmed().isEmpty() && chosenRoom.isEmpty()) return invalid(QStringLiteral("Неверная ссылка на комнату Jitsi."));
    if (!maxGroup->text().trimmed().isEmpty() && !Configs::mrtt::ValidMaxGroup(maxGroup->text().trimmed())) return invalid(QStringLiteral("Неверный ID группы MAX."));
    if (!automatic && values.size() > 6) return invalid(QStringLiteral("Укажите не больше шести хешей звонков VK."));
    QSet<QString> seen;
    for (const auto &hash : values) {
        if (hash.size() < 16 || hash.contains(QRegularExpression("[,|\\s]")) || seen.contains(hash)) return invalid(tr("Enter valid, distinct VK call hashes."));
        seen.insert(hash);
    }
    if (token->text().size() > 4096 || token->text().contains(QRegularExpression("[\\s\\x00-\\x1f\\x7f]")))
        return invalid(QStringLiteral("Некорректный токен VK."));
    if (!QRegularExpression("^[0-9]+(?:,[0-9]+){0,15}$").match(clientIds->text()).hasMatch())
        return invalid(QStringLiteral("ID клиентов VK — числа через запятую."));
    o->password = password->text();
    o->transportMode = mode->currentData().toString();
    // Another room than the link's has no server seats to go by.
    if (chosenRoom != o->jitsiRoom) o->jitsiSeats = 0;
    o->jitsiRoom = chosenRoom;
    o->jitsiWorkers = jitsiWorkers->value();
    o->maxGroup = maxGroup->text().trimmed();
    o->mtu = mtu->currentData().toInt();
    const auto previousHashSource = o->hashSource;
    o->hashSource = hashSource->currentData().toString();
    o->vkAccessToken = token->text();
    o->autoHashCount = autoHashCount->value();
    if (!automatic && (values != o->hashes || previousHashSource != "manual")) o->manualRelayHashes = true;
    o->hashes = values;
    o->workers = workers->currentData().toInt();
    o->turnTransport = transport->currentData().toString();
    o->vkAnonPath = authorization->currentData().toString();
    o->captchaMode = captcha->currentData().toString();
    o->srtpMode = srtp->currentData().toString();
    o->fingerprint = fingerprint->currentText();
    o->clientIds = clientIds->text(); o->customTurnIps = customTurnIps->text();
    return true;
}

void EditMRTT::updateWorkers(int requested) {
    QStringList values;
    for (auto *field : hashes) values.append(field->text());
    const int hashCount = hashSource->currentData() == "auto" ? autoHashCount->value() : Configs::mrtt::NormalizeHashes(values).size();
    if (requested < 0) requested = workers->currentData().toInt();
    // A VK call holds a limited number of workers; automatic calls and MAX take any count.
    const bool anyCount = mode->currentData() == "max" || hashSource->currentData() == "auto_js";
    const int selected = Configs::mrtt::NormalizeWorkers(requested, hashCount, anyCount);
    const QSignalBlocker blocker(workers);
    workers->clear();
    for (int count = Configs::mrtt::WorkersPerGroup; count <= Configs::mrtt::MaxWorkersFor(hashCount, anyCount); count += Configs::mrtt::WorkersPerGroup)
        workers->addItem(QString::number(count), count);
    workers->setCurrentIndex(workers->findData(selected));
}
