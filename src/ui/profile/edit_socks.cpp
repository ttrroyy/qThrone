#include "include/ui/profile/edit_socks.h"

EditSocks::EditSocks(QWidget *parent)
    : QWidget(parent),
      ui(new Ui::EditSocks) {

    ui->setupUi(this);
}

EditSocks::~EditSocks() {
    delete ui;
}

void EditSocks::onStart(std::shared_ptr<Configs::Profile> _ent) {
    this->ent = _ent;
    auto outbound = this->ent->Socks();

    ui->version->setCurrentText(Int2String(outbound->version));
    ui->username->setText(outbound->username);
    ui->password->setText(outbound->password);
    ui->phobos_key->setText(outbound->phobos_key);
    ui->phobos_masking->setCurrentText(outbound->phobos_masking.isEmpty() ? QStringLiteral("NONE") : outbound->phobos_masking);
    ui->phobos_media_ssrc->setText(outbound->phobos_media_ssrc > 0 ? QString::number(outbound->phobos_media_ssrc) : QString());
}

bool EditSocks::onEnd() {
    auto outbound = this->ent->Socks();

    outbound->version = ui->version->currentText().trimmed().toInt();
    outbound->username = ui->username->text().trimmed();
    outbound->password = ui->password->text();
    outbound->phobos_key = ui->phobos_key->text().trimmed();
    outbound->phobos_masking = ui->phobos_masking->currentText() == "NONE" ? QString() : ui->phobos_masking->currentText();
    outbound->phobos_media_ssrc = ui->phobos_media_ssrc->text().trimmed().toUInt(nullptr, 0);
    // The Phobos server role speaks SOCKS5 only.
    if (!outbound->phobos_key.isEmpty()) outbound->version = 5;
    return true;
}