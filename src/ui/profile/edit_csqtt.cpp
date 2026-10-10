#include "include/ui/profile/edit_csqtt.h"
#include "include/configs/outbounds/csqtt.h"
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QRegularExpression>
#include <QSet>

EditCSQTT::EditCSQTT(QWidget *parent) : QWidget(parent) {
    auto *form = new QFormLayout(this);
    password = new QLineEdit(this);
    password->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Password"), password);
    for (int i = 0; i < 6; ++i) {
        hashes[i] = new QLineEdit(this);
        hashes[i]->setObjectName(QString("csqttHash%1").arg(i + 1));
        hashes[i]->setPlaceholderText(tr("Call hash or VK call link"));
        form->addRow(tr("VK hash %1").arg(i + 1), hashes[i]);
    }
    workers = new QComboBox(this);
    workers->setObjectName("csqttWorkers");
    for (auto *field : hashes) connect(field, &QLineEdit::textChanged, this, [this] { updateWorkers(); });
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
}

void EditCSQTT::onStart(std::shared_ptr<Configs::Profile> profile) {
    ent = std::move(profile);
    auto *o = dynamic_cast<Configs::csqtt *>(ent->outbound.get());
    if (!o) return;
    password->setText(o->password);
    for (int i = 0; i < 6; ++i) hashes[i]->setText(i < o->hashes.size() ? o->hashes[i] : QString());
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
    const auto invalid = [this](const QString &message){ MessageBoxWarning("CSQTT", message); return false; };
    if (get_edit_text_serverAddress().trimmed().isEmpty()) return invalid(tr("Server address is required."));
    bool portOK;
    const int p = get_edit_text_serverPort().toInt(&portOK);
    if (!portOK || p < 1 || p > 65535) return invalid(tr("Server port must be between 1 and 65535."));
    if (password->text().isEmpty() || password->text().contains('|') || password->text().contains('\n') || password->text().contains('\r')) return invalid(tr("Enter a valid connection password."));
    if (values.isEmpty() || values.size() > 6) return invalid(tr("Enter one to six VK call hashes."));
    QSet<QString> seen;
    for (const auto &hash : values) {
        if (hash.size() < 16 || hash.contains(QRegularExpression("[,|\\s]")) || seen.contains(hash)) return invalid(tr("Enter valid, distinct VK call hashes."));
        seen.insert(hash);
    }
    if (device->text().trimmed().isEmpty() || device->text().contains('|')) return invalid(tr("Enter a valid device ID."));
    o->password = password->text();
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
    const int hashCount = Configs::csqtt::NormalizeHashes(values).size();
    if (requested < 0) requested = workers->currentData().toInt();
    const int selected = Configs::csqtt::NormalizeWorkers(requested, hashCount);
    const QSignalBlocker blocker(workers);
    workers->clear();
    for (int count = 9; count <= Configs::csqtt::MaxWorkers(hashCount); count += 9)
        workers->addItem(QString::number(count), count);
    workers->setCurrentIndex(workers->findData(selected));
}
