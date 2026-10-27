#include "include/ui/profile/edit_openfluxkey.h"
#include "include/configs/outbounds/openfluxkey.h"
#include <QFormLayout>
#include <QLabel>

namespace {
// openflux-app's transport names, in the bridge's order.
const QStringList kCaptions{QStringLiteral("Яндекс Документы"), QStringLiteral("Яндекс Волга"), "MAX", QStringLiteral("Яндекс Доски"), "Mail.ru",
                            QStringLiteral("МТС Линк"), QStringLiteral("Телемост"), "Cups.online", QStringLiteral("Авито"),
                            QStringLiteral("Яндекс Документы, несколько")};
}

EditOpenFluxKey::EditOpenFluxKey(QWidget *parent) : QWidget(parent) {
    auto *form = new QFormLayout(this);
    transport = new QComboBox(this);
    const auto &names = Configs::openfluxkey::Transports();
    for (int i = 0; i < names.size(); ++i) transport->addItem(kCaptions.value(i, names[i]), names[i]);
    form->addRow(QStringLiteral("Транспорт"), transport);
    document = new QLineEdit(this);
    document->setPlaceholderText("https://boards.yandex.ru/whiteboard/?hash=…");
    form->addRow(QStringLiteral("Ссылка на документ или доску"), document);
    key = new QLineEdit(this);
    form->addRow(QStringLiteral("Ключ доступа"), key);
    e2e = new QCheckBox(QStringLiteral("Сквозное шифрование ключом"), this);
    form->addRow(e2e);
    auto *note = new QLabel(QStringLiteral("Профили форка OpenFlux с панелью ключей (ссылки openflux://import). Если Яндекс попросит проверку, она откроется в отдельном окне браузера."), this);
    note->setWordWrap(true);
    form->addRow(note);
}

void EditOpenFluxKey::onStart(std::shared_ptr<Configs::Profile> profile) {
    ent = std::move(profile);
    auto *o = dynamic_cast<Configs::openfluxkey *>(ent->outbound.get());
    if (!o) return;
    const auto current = o->data.value("transport").toString("boards");
    transport->setCurrentIndex(qMax(0, transport->findData(current)));
    document->setText(o->data.value("doc_url").toString());
    key->setText(o->data.value("key_token").toString());
    e2e->setChecked(o->data.value("e2e_encryption").toBool());
}

bool EditOpenFluxKey::onEnd() {
    auto *o = dynamic_cast<Configs::openfluxkey *>(ent->outbound.get());
    if (!o) return false;
    // Fields this editor has no control for (from a link) are kept.
    auto updated = o->ExportToJson();
    updated["transport"] = transport->currentData().toString();
    const auto doc = document->text().trimmed();
    if (doc.isEmpty()) updated.remove("doc_url"); else updated["doc_url"] = doc;
    const auto token = key->text().trimmed();
    if (token.isEmpty()) updated.remove("key_token"); else updated["key_token"] = token;
    if (e2e->isChecked()) updated["e2e_encryption"] = true; else updated.remove("e2e_encryption");
    Configs::openfluxkey check;
    if (!check.ParseFromJson(updated)) {
        MessageBoxWarning("OpenFlux Key", check.Validate().isEmpty() ? QStringLiteral("Проверьте параметры профиля") : check.Validate());
        return false;
    }
    return o->ParseFromJson(updated);
}
