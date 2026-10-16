#include "include/ui/profile/edit_openflux.h"
#include "include/configs/outbounds/openflux.h"
#include <QFormLayout>
#include <QSet>

namespace {
// Carriers in the order the original apps list them; the first three always get a field.
const QList<QPair<QString, QString>> kCarrierFields{
    {"yandex", QStringLiteral("Документ Яндекса")},
    {"vyandex", QStringLiteral("Документ Яндекса (Volga)")},
    {"boards", QStringLiteral("Доска Яндекса")},
    {"mailru", QStringLiteral("Документ Mail.ru")},
    {"cupsonline", QStringLiteral("Документ Cups.online")},
    {"avito", QStringLiteral("Комната Авито")},
    {"telemost", QStringLiteral("Конференция Телемост")},
};
const QStringList kAlwaysShown{"yandex", "mailru", "cupsonline"};
// Priorities the earlier editor gave new carriers; context derivation prefers the highest.
int defaultPriority(const QString &type) { return type == "yandex" ? 50 : type == "mailru" ? 40 : type == "cupsonline" ? 30 : 20; }
}

EditOpenFlux::EditOpenFlux(QWidget *parent) : QWidget(parent) {
    auto *form = new QFormLayout(this);
    secret = new QLineEdit(this); secret->setEchoMode(QLineEdit::Normal);
    context = new QLineEdit(this); context->setPlaceholderText(QStringLiteral("Автоматически, как в OpenFlux"));
    form->addRow(QStringLiteral("Секрет"), secret);
    for (const auto &[type, label] : kCarrierFields) {
        auto *edit = new QLineEdit(this);
        auto *text = new QLabel(label, this);
        form->addRow(text, edit);
        carriers[type] = edit;
        carrierLabels[type] = text;
    }
    form->addRow(QStringLiteral("Контекст подключения"), context);
}

void EditOpenFlux::onStart(std::shared_ptr<Configs::Profile> profile) {
    ent = std::move(profile);
    auto *o = dynamic_cast<Configs::openflux*>(ent->outbound.get());
    if (!o) return;
    secret->setText(o->secret); context->setText(o->context);
    QSet<QString> present;
    for (const auto &entry : o->transports) {
        const auto t = entry.toObject(); const auto type = t["type"].toString();
        // The first carrier of a type is the one its field edits; further ones pass through untouched.
        if (!carriers.contains(type) || present.contains(type)) continue;
        present.insert(type);
        carriers[type]->setText(t["url"].toString());
    }
    // Rarer carriers get a field only when the profile already uses them.
    for (auto it = carriers.begin(); it != carriers.end(); ++it) {
        const bool visible = kAlwaysShown.contains(it.key()) || present.contains(it.key());
        it.value()->setVisible(visible);
        carrierLabels[it.key()]->setVisible(visible);
    }
}

bool EditOpenFlux::onEnd() {
    auto *o = dynamic_cast<Configs::openflux*>(ent->outbound.get());
    if (!o) return false;
    Configs::openflux updated;
    updated.name = o->name; updated.secret = secret->text(); updated.context = context->text().trimmed();
    updated.codec = o->codec; updated.mode = o->mode; updated.extra = o->extra;
    // A new profile is a Session; an imported one keeps exactly what its link says.
    updated.negotiate = o->transports.isEmpty() || o->negotiate;
    const auto host = get_edit_text_serverAddress().trimmed();
    const auto dial = host.isEmpty() ? QString() : (host.contains(':') ? "[" + host + "]" : host) + ":" + get_edit_text_serverPort().trimmed();
    QSet<QString> handled;
    for (const auto &entry : o->transports) {
        auto t = entry.toObject();
        const auto type = t["type"].toString();
        if (type == "direct" && !handled.contains(type)) {
            handled.insert(type);
            if (dial.isEmpty()) continue;
            t["dial"] = dial;
        } else if (carriers.contains(type) && carriers[type]->isVisibleTo(this) && !handled.contains(type)) {
            handled.insert(type);
            const auto value = carriers[type]->text().trimmed();
            if (value.isEmpty()) continue;
            t["url"] = value;
        }
        updated.transports.append(t);
    }
    if (!handled.contains("direct") && !dial.isEmpty()) updated.transports.append(QJsonObject{{"type", "direct"}, {"priority", 100}, {"dial", dial}});
    for (const auto &[type, label] : kCarrierFields) {
        const auto value = carriers[type]->text().trimmed();
        if (handled.contains(type) || value.isEmpty() || !carriers[type]->isVisibleTo(this)) continue;
        updated.transports.append(QJsonObject{{"type", type}, {"url", value}, {"priority", defaultPriority(type)}});
    }
    // Several carriers, or a direct one, only work as a Session.
    for (const auto &entry : updated.transports) {
        if (entry.toObject()["type"].toString() == "direct") updated.negotiate = true;
    }
    if (updated.transports.size() > 1) updated.negotiate = true;
    if (updated.negotiate) updated.mode.clear();
    if (const auto error = updated.Validate(); !error.isEmpty()) { MessageBoxWarning("OpenFlux", error); return false; }
    return o->ParseFromJson(updated.ExportToJson());
}
