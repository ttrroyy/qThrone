#include "include/ui/profile/edit_sstp.h"
#include "include/configs/outbounds/sstp.h"
#include <QFormLayout>
EditSSTP::EditSSTP(QWidget *parent) : QWidget(parent) {
    auto *form = new QFormLayout(this);
    username = new QLineEdit(this); password = new QLineEdit(this); password->setEchoMode(QLineEdit::Password);
    sni = new QLineEdit(this); sni->setPlaceholderText(tr("Same as server address"));
    insecure = new QCheckBox(tr("Allow insecure TLS certificate"), this);
    form->addRow(tr("Username"), username); form->addRow(tr("Password"), password);
    form->addRow("SNI", sni); form->addRow(insecure);
}
void EditSSTP::onStart(std::shared_ptr<Configs::Profile> profile) {
    ent = std::move(profile); const auto *o = dynamic_cast<Configs::sstp *>(ent->outbound.get()); if (!o) return;
    username->setText(o->username); password->setText(o->password); sni->setText(o->sni); insecure->setChecked(o->allowInsecure);
}
bool EditSSTP::onEnd() {
    auto *o = dynamic_cast<Configs::sstp *>(ent->outbound.get()); if (!o) return false;
    o->username = username->text().trimmed(); o->password = password->text(); o->sni = sni->text().trimmed(); o->allowInsecure = insecure->isChecked();
    if (o->username.isEmpty() || o->password.isEmpty()) { MessageBoxWarning("SSTP", tr("Username and password are required")); return false; }
    return true;
}
