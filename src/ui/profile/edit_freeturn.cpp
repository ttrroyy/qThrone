#include "include/ui/profile/edit_freeturn.h"
#include "include/configs/outbounds/freeturn.h"
#include <QFormLayout>
namespace {
QComboBox *combo(QWidget *parent, const QList<QPair<QString, QString>> &items) {
    auto *box = new QComboBox(parent);
    for (const auto &[value, label] : items) box->addItem(label, value);
    return box;
}
void pick(QComboBox *box, const QString &value) { if (const int index = box->findData(value); index >= 0) box->setCurrentIndex(index); }
}
EditFreeTurn::EditFreeTurn(QWidget *parent) : QWidget(parent) {
    auto *form = new QFormLayout(this);
    provider = combo(this, {{"vk", QStringLiteral("Через TURN звонка VK")}, {"direct", QStringLiteral("Напрямую к серверу")}});
    tunnelMode = combo(this, {{"udp", QStringLiteral("UDP: встроенный WireGuard")}, {"tcp", QStringLiteral("TCP: первое звено цепочки")}});
    tunnelMode->setToolTip(QStringLiteral("В режиме TCP сервер пересылает потоки своему Xray: поставьте этот профиль первым звеном цепочки перед профилем VLESS и т.п."));
    relayTransport = combo(this, {{"tcp", "TCP/TLS"}, {"udp", "UDP"}});
    obfProfile = combo(this, {{"none", QStringLiteral("Нет")}, {"rtpopus3", "rtpopus3"}, {"rtpopus2", "rtpopus2"}, {"rtpopus", "rtpopus"}});
    dnsMode = combo(this, {{"auto", QStringLiteral("Автоматически")}, {"plain", "UDP/53"}, {"doh", "DNS-over-HTTPS"}});
    links = new QPlainTextEdit(this); links->setMaximumHeight(70); links->setPlaceholderText(QStringLiteral("Одна ссылка на звонок в строке"));
    wgConfig = new QPlainTextEdit(this); wgConfig->setMinimumHeight(110); wgConfig->setPlaceholderText("[Interface]\nPrivateKey = ...\n\n[Peer]\nPublicKey = ...");
    obfKey = new QLineEdit(this); clientId = new QLineEdit(this); dnsServers = new QLineEdit(this); turnHost = new QLineEdit(this);
    clientId->setPlaceholderText(QStringLiteral("Создаётся автоматически"));
    threads = new QSpinBox(this); threads->setRange(0, 256);
    streamsPerCred = new QSpinBox(this); streamsPerCred->setRange(0, 256);
    obfTiming = new QSpinBox(this); obfTiming->setRange(0, 1000);
    bond = new QCheckBox(QStringLiteral("Объединять сессии (bond)"), this);
    manualCaptcha = new QCheckBox(QStringLiteral("Ручная капча"), this);
    form->addRow(QStringLiteral("Источник потоков"), provider);
    form->addRow(QStringLiteral("Ссылки на звонки VK"), links);
    form->addRow(QStringLiteral("Конфигурация WireGuard или AmneziaWG"), wgConfig);
    form->addRow(QStringLiteral("Режим"), tunnelMode);
    form->addRow(bond);
    form->addRow(QStringLiteral("Транспорт до TURN"), relayTransport);
    form->addRow(QStringLiteral("Потоки"), threads);
    form->addRow(QStringLiteral("Потоков на учётку"), streamsPerCred);
    form->addRow(QStringLiteral("Обфускация"), obfProfile);
    form->addRow(QStringLiteral("Ключ обфускации (64 HEX)"), obfKey);
    form->addRow(QStringLiteral("Задержка обфускации, мс"), obfTiming);
    form->addRow("Client ID", clientId);
    form->addRow(QStringLiteral("DNS клиента"), dnsMode);
    form->addRow(QStringLiteral("DNS-серверы клиента"), dnsServers);
    form->addRow(QStringLiteral("TURN-сервер (необязательно)"), turnHost);
    form->addRow(manualCaptcha);
}
void EditFreeTurn::onStart(std::shared_ptr<Configs::Profile> profile) {
    ent = std::move(profile); const auto *o = dynamic_cast<Configs::freeturn *>(ent->outbound.get()); if (!o) return;
    pick(provider, o->provider); pick(tunnelMode, o->tunnelMode); pick(relayTransport, o->relayTransport); pick(obfProfile, o->obfProfile); pick(dnsMode, o->dnsMode);
    links->setPlainText(o->links); wgConfig->setPlainText(o->wgConfig); obfKey->setText(o->obfKey); clientId->setText(o->clientId);
    dnsServers->setText(o->dnsServers); turnHost->setText(o->turnHost); threads->setValue(o->threads); streamsPerCred->setValue(o->streamsPerCred);
    obfTiming->setValue(o->obfTiming); bond->setChecked(o->bond); manualCaptcha->setChecked(o->manualCaptcha);
}
bool EditFreeTurn::onEnd() {
    auto *o = dynamic_cast<Configs::freeturn *>(ent->outbound.get()); if (!o) return false;
    o->provider = provider->currentData().toString(); o->tunnelMode = tunnelMode->currentData().toString();
    o->relayTransport = relayTransport->currentData().toString(); o->obfProfile = obfProfile->currentData().toString(); o->dnsMode = dnsMode->currentData().toString();
    o->links = links->toPlainText().trimmed(); o->wgConfig = wgConfig->toPlainText().trimmed(); o->obfKey = obfKey->text().trimmed(); o->clientId = clientId->text().trimmed();
    o->dnsServers = dnsServers->text().trimmed(); o->turnHost = turnHost->text().trimmed(); o->threads = threads->value(); o->streamsPerCred = streamsPerCred->value();
    o->obfTiming = obfTiming->value(); o->bond = bond->isChecked(); o->manualCaptcha = manualCaptcha->isChecked();
    if (const auto error = o->Validate(); !error.isEmpty()) { MessageBoxWarning("FreeTurn", error); return false; }
    return true;
}
