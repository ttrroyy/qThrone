#include "include/ui/profile/edit_qwdtt.h"
#include "include/configs/outbounds/qwdtt.h"
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>

EditQWDTT::EditQWDTT(QWidget *parent) : QWidget(parent) {
    auto *form = new QFormLayout(this);
    mode = new QComboBox(this);
    mode->addItem("RAW", "raw");
    mode->addItem("WG", "wg");
    form->addRow(tr("Tunnel mode"), mode);
    password = new QLineEdit(this);
    password->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Password"), password);
    for (int i = 0; i < 4; ++i) {
        hashes[i] = new QLineEdit(this);
        hashes[i]->setObjectName(QString("qwdttHash%1").arg(i + 1));
        hashes[i]->setPlaceholderText(tr("Call hash or VK call link"));
        form->addRow(tr("VK hash %1").arg(i + 1), hashes[i]);
    }
    rawPort = new QSpinBox(this);
    rawPort->setRange(1, 65535);
    rawPort->setValue(56003);
    form->addRow(tr("RAW server port"), rawPort);
    rawPort->setToolTip(tr("qWDTT RAW listener, normally 56003. The main server port is used by WG."));
    workers = new QComboBox(this);
    workers->setObjectName("qwdttWorkers");
    for (auto *field : hashes) connect(field, &QLineEdit::textChanged, this, [this] { updateWorkers(); });
    updateWorkers(9);
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
    connect(mode, &QComboBox::currentIndexChanged, this, [this](int){ rawPort->setEnabled(mode->currentData() == "raw"); });
}

void EditQWDTT::onStart(std::shared_ptr<Configs::Profile> profile) {
    ent = std::move(profile);
    auto *o = dynamic_cast<Configs::qwdtt *>(ent->outbound.get());
    if (!o) return;
    mode->setCurrentIndex(o->mode == "wg" ? 1 : 0);
    password->setText(o->password);
    for (int i = 0; i < 4; ++i) hashes[i]->setText(i < o->hashes.size() ? o->hashes[i] : QString());
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
    if (!o) return false;
    QStringList values;
    for (auto *field : hashes) values.append(field->text());
    values = Configs::qwdtt::NormalizeHashes(values);
    const auto invalid = [this](const QString &message){ MessageBoxWarning("qWDTT", message); return false; };
    if (get_edit_text_serverAddress().trimmed().isEmpty()) return invalid(tr("Server address is required."));
    bool portOK;
    const int p = get_edit_text_serverPort().toInt(&portOK);
    if (!portOK || p < 1 || p > 65535) return invalid(tr("Server port must be between 1 and 65535."));
    if (password->text().isEmpty() || password->text().contains('|') || password->text().contains('\n') || password->text().contains('\r')) return invalid(tr("Enter a valid connection password."));
    if (values.isEmpty() || values.size() > 4) return invalid(tr("Enter one to four VK call hashes."));
    if (device->text().trimmed().isEmpty() || device->text().contains('|')) return invalid(tr("Enter a valid device ID."));
    o->mode = mode->currentData().toString();
    o->password = password->text();
    o->hashes = values;
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
    for (auto *field : hashes) values.append(field->text());
    const int hashCount = Configs::qwdtt::NormalizeHashes(values).size();
    if (requested < 0) requested = workers->currentData().toInt();
    const int selected = Configs::qwdtt::NormalizeWorkers(requested, hashCount);
    const QSignalBlocker blocker(workers);
    workers->clear();
    for (int count = 9; count <= Configs::qwdtt::MaxWorkers(hashCount); count += 9)
        workers->addItem(QString::number(count), count);
    workers->setCurrentIndex(workers->findData(selected));
}
