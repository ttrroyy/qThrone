#include "include/ui/profile/edit_dnstunnel.h"
#include "include/configs/outbounds/dnstunnel.h"
#include <QFormLayout>
#include <QJsonArray>
#include <QRegularExpression>

EditDnsTunnel::EditDnsTunnel(QWidget *parent) : QWidget(parent) {
    form = new QFormLayout(this);
    const auto multiline = [this](int lines) {
        auto *edit = new QPlainTextEdit(this);
        edit->setTabChangesFocus(true);
        edit->setFixedHeight(edit->fontMetrics().lineSpacing() * lines + 14);
        return edit;
    };
    domain = new QLineEdit(this); domain->setPlaceholderText("t.example.com");
    pubkey = new QLineEdit(this); pubkey->setPlaceholderText(QStringLiteral("64 шестнадцатеричных символа"));
    cert = multiline(3); cert->setPlaceholderText(QStringLiteral("-----BEGIN CERTIFICATE----- … (необязательно)"));
    mkey = new QLineEdit(this);
    encryption = new QComboBox(this);
    const QStringList methods{QStringLiteral("Нет"), "XOR", "ChaCha20", "AES-128-GCM", "AES-192-GCM", "AES-256-GCM"};
    for (int i = 0; i < methods.size(); ++i) encryption->addItem(methods[i], i);
    resolvers = multiline(3);
    recordType = new QComboBox(this);
    for (const auto &type : Configs::dnstunnel::RecordTypes()) recordType->addItem(type.toUpper(), type);
    dnsttCompat = new QCheckBox(QStringLiteral("Совместимость с DNSTT"), this);
    clientIdSize = new QSpinBox(this); clientIdSize->setRange(1, 64); clientIdSize->setValue(2);
    idleTimeout = new QLineEdit(this); idleTimeout->setPlaceholderText(QStringLiteral("по умолчанию, например 60s"));
    keepalive = new QLineEdit(this); keepalive->setPlaceholderText(QStringLiteral("по умолчанию, например 10s"));
    authoritative = new QCheckBox(QStringLiteral("Резолвер — авторитетный сервер"), this);
    congestion = new QComboBox(this); congestion->addItem("BBR", "bbr"); congestion->addItem("DCUBIC", "dcubic");
    keepAliveInterval = new QSpinBox(this); keepAliveInterval->setRange(1, 65535); keepAliveInterval->setValue(400);
    backend = new QComboBox(this);
    backend->addItem("SOCKS5", "socks"); backend->addItem("SSH", "ssh"); backend->addItem("Shadowsocks", "shadowsocks");
    user = new QLineEdit(this);
    password = new QLineEdit(this);
    privateKey = multiline(3); privateKey->setPlaceholderText(QStringLiteral("-----BEGIN OPENSSH PRIVATE KEY----- … (необязательно)"));
    method = new QComboBox(this);
    for (const auto *m : {"chacha20-ietf-poly1305", "aes-256-gcm", "aes-128-gcm"}) method->addItem(m, m);
    note = new QLabel(this); note->setWordWrap(true);

    form->addRow(QStringLiteral("Домен туннеля"), domain);
    form->addRow(QStringLiteral("Публичный ключ сервера"), pubkey);
    form->addRow(QStringLiteral("Сертификат сервера (PEM)"), cert);
    form->addRow(QStringLiteral("Ключ шифрования"), mkey);
    form->addRow(QStringLiteral("Шифрование"), encryption);
    form->addRow(QStringLiteral("DNS-резолверы"), resolvers);
    form->addRow(QStringLiteral("Тип DNS-записи"), recordType);
    form->addRow(QString(), dnsttCompat);
    form->addRow(QStringLiteral("Размер ClientID, байт"), clientIdSize);
    form->addRow(QStringLiteral("Тайм-аут простоя"), idleTimeout);
    form->addRow(QStringLiteral("Интервал keepalive"), keepalive);
    form->addRow(QString(), authoritative);
    form->addRow(QStringLiteral("Управление перегрузкой"), congestion);
    form->addRow(QStringLiteral("Keepalive QUIC, мс"), keepAliveInterval);
    form->addRow(QStringLiteral("Сервис на сервере"), backend);
    form->addRow(QStringLiteral("Логин"), user);
    form->addRow(QStringLiteral("Пароль"), password);
    form->addRow(QStringLiteral("Приватный ключ SSH"), privateKey);
    form->addRow(QStringLiteral("Метод шифрования"), method);
    form->addRow(note);
    connect(backend, &QComboBox::currentIndexChanged, this, [this] { updateFields(); });
}

void EditDnsTunnel::setRowVisible(QWidget *field, bool visible) {
    field->setVisible(visible);
    if (auto *label = form->labelForField(field)) label->setVisible(visible);
}

void EditDnsTunnel::updateFields() {
    const bool dnstt = kind == "dnstt", vaydns = kind == "vaydns", slip = kind == "slipstream", master = kind == "masterdns";
    setRowVisible(pubkey, dnstt || vaydns);
    setRowVisible(cert, slip);
    setRowVisible(mkey, master);
    setRowVisible(encryption, master);
    for (QWidget *field : std::initializer_list<QWidget *>{recordType, dnsttCompat, clientIdSize, idleTimeout, keepalive}) setRowVisible(field, vaydns);
    for (QWidget *field : std::initializer_list<QWidget *>{authoritative, congestion, keepAliveInterval}) setRowVisible(field, slip);
    const auto service = backend->currentData().toString();
    setRowVisible(backend, !master);
    setRowVisible(user, !master && service != "shadowsocks");
    setRowVisible(password, !master);
    setRowVisible(privateKey, !master && service == "ssh");
    setRowVisible(method, !master && service == "shadowsocks");
    resolvers->setPlaceholderText(slip || master ? QStringLiteral("По одному в строке: IP или IP:порт (UDP)")
                                                 : QStringLiteral("По одному в строке: IP или IP:порт (UDP), https://… (DoH), tls://… (DoT)"));
    note->setText(QStringLiteral("Без резолверов используются DNS-серверы текущей сети.") +
                  (master ? QString() : QStringLiteral(" Через туннель передаётся только TCP, DNS-запросы идут по TCP.")));
}

void EditDnsTunnel::onStart(std::shared_ptr<Configs::Profile> profile) {
    ent = std::move(profile);
    auto *o = dynamic_cast<Configs::dnstunnel *>(ent->outbound.get());
    if (!o) return;
    kind = ent->type;
    const auto &d = o->data;
    domain->setText(d["domain"].toString());
    pubkey->setText(d["pubkey"].toString());
    cert->setPlainText(d["cert"].toString());
    mkey->setText(d["mkey"].toString());
    encryption->setCurrentIndex(qBound(0, d["encryption"].toInt(1), 5));
    QStringList list;
    for (const auto &value : d["resolvers"].toArray()) list << value.toString();
    resolvers->setPlainText(list.join('\n'));
    recordType->setCurrentIndex(qMax(0, recordType->findData(d["record_type"].toString("txt"))));
    dnsttCompat->setChecked(d["dnstt_compat"].toBool());
    clientIdSize->setValue(d["clientid_size"].toInt(2));
    idleTimeout->setText(d["idle_timeout"].toString());
    keepalive->setText(d["keepalive"].toString());
    authoritative->setChecked(d["authoritative"].toBool());
    congestion->setCurrentIndex(qMax(0, congestion->findData(d["congestion_control"].toString("bbr"))));
    keepAliveInterval->setValue(d["keep_alive_interval"].toInt(400));
    const auto service = d["backend"].toObject();
    backend->setCurrentIndex(qMax(0, backend->findData(service["type"].toString("socks"))));
    user->setText(service["user"].toString());
    password->setText(service["password"].toString());
    privateKey->setPlainText(service["pk"].toString());
    method->setCurrentIndex(qMax(0, method->findData(service["method"].toString("chacha20-ietf-poly1305"))));
    updateFields();
}

bool EditDnsTunnel::onEnd() {
    auto *o = dynamic_cast<Configs::dnstunnel *>(ent->outbound.get());
    if (!o) return false;
    // Fields this editor has no control for (from a link or JSON) are kept.
    auto d = o->data;
    for (const auto *key : {"domain", "pubkey", "cert", "mkey", "encryption", "resolvers", "record_type", "dnstt_compat",
                            "clientid_size", "idle_timeout", "keepalive", "authoritative", "congestion_control", "keep_alive_interval", "backend"})
        d.remove(QString::fromLatin1(key));
    d["domain"] = domain->text().trimmed();
    QJsonArray list;
    for (const auto &line : resolvers->toPlainText().split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts)) list.append(line);
    if (!list.isEmpty()) d["resolvers"] = list;
    if (kind == "dnstt" || kind == "vaydns") d["pubkey"] = pubkey->text().trimmed();
    if (kind == "slipstream") {
        if (!cert->toPlainText().trimmed().isEmpty()) d["cert"] = cert->toPlainText().trimmed();
        d["authoritative"] = authoritative->isChecked();
        d["congestion_control"] = congestion->currentData().toString();
        d["keep_alive_interval"] = keepAliveInterval->value();
    }
    if (kind == "vaydns") {
        d["record_type"] = recordType->currentData().toString();
        d["dnstt_compat"] = dnsttCompat->isChecked();
        d["clientid_size"] = clientIdSize->value();
        if (!idleTimeout->text().trimmed().isEmpty()) d["idle_timeout"] = idleTimeout->text().trimmed();
        if (!keepalive->text().trimmed().isEmpty()) d["keepalive"] = keepalive->text().trimmed();
    }
    if (kind == "masterdns") {
        d["mkey"] = mkey->text().trimmed();
        d["encryption"] = encryption->currentData().toInt();
    } else {
        // Keep only what the chosen service uses, so a link shares no stale password or key.
        const auto service = backend->currentData().toString();
        QJsonObject b{{"type", service}};
        if (service != "shadowsocks" && !user->text().isEmpty()) b["user"] = user->text();
        if (!password->text().isEmpty()) b["password"] = password->text();
        if (service == "ssh" && !privateKey->toPlainText().trimmed().isEmpty()) b["pk"] = privateKey->toPlainText().trimmed();
        if (service == "shadowsocks") b["method"] = method->currentData().toString();
        d["backend"] = b;
    }
    Configs::dnstunnel updated(kind);
    updated.name = o->name;
    updated.data = d;
    QJsonObject json = updated.ExportToJson();
    if (!updated.ParseFromJson(json)) {
        MessageBoxWarning(Configs::dnstunnel::Title(kind), updated.Validate());
        return false;
    }
    return o->ParseFromJson(updated.ExportToJson());
}
