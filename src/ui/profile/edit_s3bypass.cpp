#include "include/ui/profile/edit_s3bypass.h"
#include "include/configs/outbounds/s3bypass.h"
#include <QFormLayout>
#include <QLabel>

namespace {
// The original app's tuning; both sides keep their own.
struct Tuning { const char *key; const char *label; int minimum, maximum, value; };
constexpr Tuning kTuning[] = {
    {"pollIntervalMs", "Опрос хранилища, мс", 1, 60000, 100},
    {"writeIntervalMs", "Запись в хранилище, мс", 1, 60000, 20},
    {"idleTimeoutSec", "Тайм-аут простоя, с", 1, 86400, 300},
    {"maxFileSizeBytes", "Наибольший объект, байт", 1024, 1 << 30, 2097152},
};
}

EditS3Bypass::EditS3Bypass(QWidget *parent) : QWidget(parent) {
    auto *form = new QFormLayout(this);
    const QList<QPair<QString, QString>> fields{
        {"endpoint", QStringLiteral("Адрес хранилища (endpoint)")}, {"region", QStringLiteral("Регион")},
        {"bucket", QStringLiteral("Корзина (bucket)")}, {"prefix", QStringLiteral("Префикс")},
        {"sessionsDir", QStringLiteral("Каталог сессий")}, {"accessKey", "Access Key"}, {"secretKey", "Secret Key"}};
    for (const auto &[key, label] : fields) {
        auto *edit = new QLineEdit(this);
        storage[key] = edit;
        form->addRow(label, edit);
    }
    storage["endpoint"]->setPlaceholderText("https://storage.example.ru");
    storage["sessionsDir"]->setText("sessions");
    for (const auto &t : kTuning) {
        auto *spin = new QSpinBox(this);
        spin->setRange(t.minimum, t.maximum);
        spin->setValue(t.value);
        tuning[QString::fromLatin1(t.key)] = spin;
        form->addRow(QString::fromUtf8(t.label), spin);
    }
    auto *note = new QLabel(QStringLiteral("Доступ к корзине S3 и префикс выдаёт сервер S3 Bypass. Параметры опроса и записи совпадают с оригинальным клиентом."), this);
    note->setWordWrap(true);
    form->addRow(note);
}

void EditS3Bypass::onStart(std::shared_ptr<Configs::Profile> profile) {
    ent = std::move(profile);
    auto *o = dynamic_cast<Configs::s3bypass *>(ent->outbound.get());
    if (!o) return;
    // value(), not operator[]: reading must not add empty keys to the profile.
    for (auto it = storage.begin(); it != storage.end(); ++it)
        it.value()->setText(o->storage.value(it.key()).toString(it.key() == "sessionsDir" ? QStringLiteral("sessions") : QString()));
    for (const auto &t : kTuning) {
        const auto key = QString::fromLatin1(t.key);
        tuning[key]->setValue(o->tuning.value(key).toInt(t.value));
    }
}

bool EditS3Bypass::onEnd() {
    auto *o = dynamic_cast<Configs::s3bypass *>(ent->outbound.get());
    if (!o) return false;
    Configs::s3bypass updated;
    updated.name = o->name;
    updated.storage = o->storage;
    updated.tuning = o->tuning;
    for (auto it = storage.begin(); it != storage.end(); ++it) {
        const auto value = it.value()->text().trimmed();
        if (value.isEmpty()) updated.storage.remove(it.key());
        else updated.storage[it.key()] = value;
    }
    if (!updated.storage.contains("type")) updated.storage["type"] = "s3";
    for (const auto &t : kTuning) updated.tuning[QString::fromLatin1(t.key)] = tuning[QString::fromLatin1(t.key)]->value();
    if (const auto error = updated.Validate(); !error.isEmpty()) { MessageBoxWarning("S3 Bypass", error); return false; }
    return o->ParseFromJson(updated.ExportToJson());
}
