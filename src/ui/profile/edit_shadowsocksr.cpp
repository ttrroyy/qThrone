#include "include/ui/profile/edit_shadowsocksr.h"
#include "include/configs/outbounds/shadowsocksr.h"
#include "include/ui/utils/ValidationForm.h"
#include <QFormLayout>

EditShadowsocksR::EditShadowsocksR(QWidget *parent) : QWidget(parent) {
    auto *form = new QFormLayout(this);
    method = new QComboBox(this); method->addItems(Configs::shadowsocksrMethods);
    protocol = new QComboBox(this); protocol->addItems(Configs::shadowsocksrProtocols);
    obfs = new QComboBox(this); obfs->addItems(Configs::shadowsocksrObfs);
    password = new QLineEdit(this); password->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    protocolParam = new QLineEdit(this);
    obfsParam = new QLineEdit(this);
    form->addRow(tr("Method"), method);
    form->addRow(tr("Password"), password);
    form->addRow(tr("Protocol"), protocol);
    form->addRow(QStringLiteral("Параметр протокола"), protocolParam);
    form->addRow(QStringLiteral("Маскировка"), obfs);
    form->addRow(QStringLiteral("Параметр маскировки"), obfsParam);
}

void EditShadowsocksR::onStart(std::shared_ptr<Configs::Profile> profile) {
    ent = std::move(profile);
    const auto *out = dynamic_cast<Configs::shadowsocksr *>(ent->outbound.get());
    method->setCurrentText(out->method);
    password->setText(out->password);
    protocol->setCurrentText(out->protocol);
    protocolParam->setText(out->protocol_param);
    obfs->setCurrentText(out->obfs);
    obfsParam->setText(out->obfs_param);
}

bool EditShadowsocksR::onEnd() {
    auto *out = dynamic_cast<Configs::shadowsocksr *>(ent->outbound.get());
    out->method = method->currentText();
    out->password = password->text();
    out->protocol = protocol->currentText();
    out->protocol_param = protocolParam->text();
    out->obfs = obfs->currentText();
    out->obfs_param = obfsParam->text();
    return true;
}

void EditShadowsocksR::bindValidation(Validation::Form &form) {
    form.bind(method, Validation::Rules::OneOf(Configs::shadowsocksrMethods, Validation::Empty::Required), this);
    form.bind(protocol, Validation::Rules::OneOf(Configs::shadowsocksrProtocols, Validation::Empty::Required), this);
    form.bind(obfs, Validation::Rules::OneOf(Configs::shadowsocksrObfs, Validation::Empty::Required), this);
    form.addRule({password, method}, [this] {
        return method->currentText() == "none" ? QString() : Validation::Required(password->text());
    }, this);
}
