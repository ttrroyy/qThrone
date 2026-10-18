#include "include/ui/setting/dialog_servers.h"
#include "include/configs/outbounds/qwdtt.h"
#include "include/configs/outbounds/wdttplus.h"
#include "include/global/Utils.hpp"
#include "include/ui/setting/dialog_vk_relay.h"
#include "include/ui/setting/dialog_wdttplus_exit.h"
#include "include/ui/utils/ShareBlocks.h"
#include <QCheckBox>
#include <QCommandLinkButton>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <optional>

namespace ServerLinks {
QString QueryEscape(const QString &value) {
    QString out;
    for (const auto c : value.toUtf8()) {
        const auto u = static_cast<unsigned char>(c);
        if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '-' || u == '_' || u == '.' || u == '~')
            out += QChar(u);
        else if (u == ' ')
            out += '+';
        else
            out += QString("%%1").arg(u, 2, 16, QChar('0')).toUpper();
    }
    return out;
}

QString Qwdtt(const QJsonObject &server, const QString &name, const QString &password, const QStringList &hashes) {
    auto host = server["host"].toString().trimmed();
    if (host.contains(':') && !host.startsWith('[')) host = "[" + host + "]";
    return "qwdtt://config?name=" + QueryEscape(name.isEmpty() ? password : name) + "&peer=" + QueryEscape(host + ":" + server["peerPort"].toString()) +
           "&hashes=" + QueryEscape(hashes.join(',')) + "&workers=18&port=9000&pass=" + QueryEscape(password);
}

QString LegacyWdtt(const QJsonObject &server, const QString &password, const QStringList &hashes) {
    const auto wg = server["wgPort"].toString().isEmpty() ? QStringLiteral("56001") : server["wgPort"].toString();
    return "wdtt://" + server["host"].toString().trimmed() + ":" + server["peerPort"].toString() + ":" + wg + ":9000:" + password + ":" + hashes.join(',');
}

bool IsOwnerAccess(const QJsonObject &server, const QString &password) {
    return !password.isEmpty() && password == server["password"].toString();
}
}

namespace {
// A text field, or the fallback when the server left it empty or out.
QString textOr(const QJsonValue &value, const QString &fallback) {
    const auto text = value.toString();
    return text.isEmpty() ? fallback : text;
}

QString bytesText(double bytes) {
    const char *units[] = {"Б", "КБ", "МБ", "ГБ", "ТБ"};
    int unit = 0;
    while (bytes >= 1024 && unit < 4) { bytes /= 1024; ++unit; }
    return QString::number(bytes, 'f', unit == 0 ? 0 : 1) + " " + QString::fromUtf8(units[unit]);
}

QString dateText(qint64 seconds, const QString &none = QStringLiteral("бессрочно")) {
    return seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds).toString("dd.MM.yyyy") : none;
}

QString timeText(qint64 seconds) {
    return seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds).toString("dd.MM.yyyy HH:mm") : QStringLiteral("—");
}

QStringList hashList(const QString &value) {
    QStringList out;
    for (auto item : value.split(QRegularExpression("[,;\\n]"), Qt::SkipEmptyParts))
        if (!(item = item.trimmed()).isEmpty()) out.append(item);
    return out;
}

QTableWidgetItem *cell(const QString &text, const QVariant &key = {}) {
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    if (key.isValid()) item->setData(Qt::UserRole, key);
    return item;
}

QTableWidget *table(QWidget *parent, const QStringList &headers) {
    auto *view = new QTableWidget(0, int(headers.size()), parent);
    view->setHorizontalHeaderLabels(headers);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->verticalHeader()->setVisible(false);
    view->horizontalHeader()->setStretchLastSection(true);
    view->setAlternatingRowColors(true);
    return view;
}

// Up to four hash fields; a pasted VK call link is read as its hash.
class HashFields : public QWidget {
public:
    QList<QLineEdit *> fields;
    HashFields(QWidget *parent, const QStringList &initial) : QWidget(parent) {
        auto *column = new QVBoxLayout(this);
        column->setContentsMargins(0, 0, 0, 0);
        for (int i = 0; i < 4; ++i) {
            auto *field = new QLineEdit(i < initial.size() ? initial[i] : QString(), this);
            field->setPlaceholderText(QStringLiteral("VK хеш или ссылка на звонок"));
            fields.append(field);
            column->addWidget(field);
        }
    }
    QStringList hashes() const {
        QStringList values;
        for (auto *field : fields) values.append(field->text());
        return Configs::qwdtt::NormalizeHashes(values).mid(0, 4);
    }
};

// One server screen: calls go through the core's relay helper; failures land in the status line.
class ServerScreen : public QDialog {
protected:
    QJsonObject server;
    RelayTask *task;
    QProgressBar *busy;
    QLabel *status;
    QList<QWidget *> actions;
    int running = 0;

    ServerScreen(QWidget *parent, QJsonObject value, const QString &title) : QDialog(parent), server(std::move(value)) {
        setWindowTitle(title + " — " + textOr(server["name"], server["host"].toString()));
        setAttribute(Qt::WA_DeleteOnClose);
        task = new RelayTask(this);
        busy = new QProgressBar(this);
        busy->setRange(0, 0);
        busy->setMaximumHeight(6);
        busy->setTextVisible(false);
        busy->hide();
        status = new QLabel(this);
        status->setWordWrap(true);
        status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }

    void call(const QString &operation, const QJsonObject &value, const std::function<void(const QJsonValue &)> &done) {
        if (++running == 1) {
            busy->show();
            for (auto *widget : actions) widget->setEnabled(false);
        }
        status->clear();
        auto result = std::make_shared<std::optional<QJsonValue>>();
        task->start({{"operation", operation}, {"server", server}, {"value", QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact))}},
                    [this, result, done](const QJsonObject &event) {
                        const auto type = event["type"].toString();
                        if (type == "result") *result = event["value"];
                        if (type == "error") status->setText(event["text"].toString());
                        if (type != "finished") return;
                        if (--running == 0) {
                            busy->hide();
                            for (auto *widget : actions) widget->setEnabled(true);
                        }
                        if (result->has_value()) done(**result);
                    });
    }

    void wdttPlus(const QStringList &args, const std::function<void(const QJsonObject &)> &done, int timeout = 45) {
        call("wdttplus-admin", {{"args", QJsonArray::fromStringList(args)}, {"timeout", timeout}}, [done](const QJsonValue &value) { done(value.toObject()); });
    }

    QPushButton *button(const QString &text, QLayout *layout, const std::function<void()> &clicked) {
        auto *b = new QPushButton(text, this);
        if (clicked) connect(b, &QPushButton::clicked, this, clicked);
        layout->addWidget(b);
        actions.append(b);
        return b;
    }

    bool confirm(const QString &title, const QString &text) {
        return QMessageBox::question(this, title, text) == QMessageBox::Yes;
    }
};

// A modal form with OK/Cancel around a QFormLayout.
struct FormDialog {
    QDialog dialog;
    QFormLayout *form;
    QVBoxLayout *layout;
    FormDialog(QWidget *parent, const QString &title) : dialog(parent) {
        dialog.setWindowTitle(title);
        dialog.setMinimumWidth(520);
        layout = new QVBoxLayout(&dialog);
        form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        layout->addLayout(form);
    }
    bool exec(const std::function<bool()> &valid = {}) {
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [this, valid] { if (!valid || valid()) dialog.accept(); });
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        return dialog.exec() == QDialog::Accepted;
    }
};

QSpinBox *spin(QWidget *parent, int minimum, int maximum, int value) {
    auto *box = new QSpinBox(parent);
    box->setRange(minimum, maximum);
    box->setValue(value);
    return box;
}

// «Пользователи и подписки» of a qWDTT server: the original app's access list over its admin API.
class QwdttAccess : public ServerScreen {
    QLineEdit *search;
    QTableWidget *view;
    QJsonArray items;

    QJsonObject selected() const {
        const auto row = view->currentRow();
        if (row < 0) return {};
        const auto password = view->item(row, 0)->data(Qt::UserRole).toString();
        for (const auto &entry : items)
            if (entry.toObject()["password"].toString() == password) return entry.toObject();
        return {};
    }

    void admin(const QString &path, const QJsonObject &form, const std::function<void(const QJsonObject &)> &done) {
        QJsonObject call{{"path", path}};
        if (!form.isEmpty() || path != "/admin/passwords") call["form"] = form;
        ServerScreen::call("qwdtt-admin", call, [done](const QJsonValue &value) { done(value.toObject()); });
    }

    void refresh() {
        admin("/admin/passwords", {}, [this](const QJsonObject &value) {
            items = {};
            for (const auto &entry : value["passwords"].toArray())
                if (!ServerLinks::IsOwnerAccess(server, entry.toObject()["password"].toString())) items.append(entry);
            render();
            if (items.isEmpty()) status->setText(QStringLiteral("Доступов пока нет. Создайте первый кнопкой «Новый доступ»."));
        });
    }

    void render() {
        const auto query = search->text().trimmed();
        view->setRowCount(0);
        for (const auto &entry : items) {
            const auto o = entry.toObject();
            const auto label = o["label"].toString(), password = o["password"].toString();
            if (!query.isEmpty() && !label.contains(query, Qt::CaseInsensitive) && !password.contains(query, Qt::CaseInsensitive) &&
                !o["vk_hash"].toString().contains(query, Qt::CaseInsensitive))
                continue;
            const int row = view->rowCount();
            view->insertRow(row);
            const auto devices = o["device_ids"].toArray().size();
            const QString state = o["is_deactivated"].toBool() ? QStringLiteral("выключен") : o["active_devices"].toInt() > 0 ? QStringLiteral("активен") : QString();
            view->setItem(row, 0, cell(label.isEmpty() ? password : label, password));
            view->setItem(row, 1, cell(password));
            view->setItem(row, 2, cell(dateText(qint64(o["expires_at"].toDouble()))));
            view->setItem(row, 3, cell(QString("%1/%2").arg(devices).arg(o["max_devices"].toInt(1))));
            view->setItem(row, 4, cell(bytesText(o["up_bytes"].toDouble() + o["down_bytes"].toDouble())));
            view->setItem(row, 5, cell(state));
        }
        view->resizeColumnsToContents();
    }

    // Create or edit: name, up to four VK hashes (sent comma-joined), days and devices.
    void form(const QJsonObject &existing) {
        const bool fresh = existing.isEmpty();
        FormDialog dialog(this, fresh ? QStringLiteral("Новый пароль доступа") : QStringLiteral("Изменить доступ"));
        auto *label = new QLineEdit(existing["label"].toString(), &dialog.dialog);
        dialog.form->addRow(QStringLiteral("Имя (кому выдан)"), label);
        auto *hashes = new HashFields(&dialog.dialog, hashList(existing["vk_hash"].toString()));
        dialog.form->addRow(QStringLiteral("VK хеши"), hashes);
        auto *days = new QLineEdit(fresh ? QStringLiteral("30") : QString(), &dialog.dialog);
        days->setPlaceholderText(fresh ? QString() : QStringLiteral("пусто — без изменений"));
        dialog.form->addRow(fresh ? QStringLiteral("Дней") : QStringLiteral("Продлить на дней"), days);
        auto *devices = spin(&dialog.dialog, 1, 100, existing["max_devices"].toInt(1));
        dialog.form->addRow(QStringLiteral("Устройств"), devices);
        bool daysOK = false;
        int dayCount = 0;
        if (!dialog.exec([&] {
                dayCount = days->text().trimmed().toInt(&daysOK);
                if ((fresh || !days->text().trimmed().isEmpty()) && (!daysOK || dayCount < 1 || dayCount > 3650)) {
                    QMessageBox::warning(&dialog.dialog, windowTitle(), QStringLiteral("Дней: от 1 до 3650"));
                    return false;
                }
                return true;
            }))
            return;
        QJsonObject values{{"vk_hash", hashes->hashes().join(',')}, {"max_devices", QString::number(devices->value())}, {"label", label->text().trimmed()}};
        if (daysOK) values["days"] = QString::number(dayCount);
        if (fresh) {
            if (values["label"].toString().isEmpty()) values.remove("label");
            admin("/admin/passwords", values, [this](const QJsonObject &created) { refresh(); details(created); });
        } else {
            values["password"] = existing["password"];
            admin("/admin/passwords/update", values, [this](const QJsonObject &updated) { refresh(); details(updated); });
        }
    }

    // The access with its QR code, both share links, password and bound devices.
    void details(const QJsonObject &access) {
        if (access.isEmpty()) return;
        const auto password = access["password"].toString();
        const auto hashes = hashList(access["vk_hash"].toString());
        const auto title = access["label"].toString().isEmpty() ? password : access["label"].toString();
        QDialog dialog(this);
        dialog.setWindowTitle(title);
        dialog.resize(640, 760);
        auto *outer = new QVBoxLayout(&dialog);
        auto *scroll = new QScrollArea(&dialog);
        scroll->setWidgetResizable(true);
        outer->addWidget(scroll);
        auto *body = new QWidget(scroll);
        scroll->setWidget(body);
        auto *layout = new QVBoxLayout(body);
        const auto link = ServerLinks::Qwdtt(server, title, password, hashes);
        layout->addWidget(ShareBlocks::Text(body, QStringLiteral("Ссылка qWDTT"), link));
        layout->addWidget(ShareBlocks::Qr(body, QStringLiteral("QR-код"), link));
        layout->addWidget(ShareBlocks::Text(body, QStringLiteral("Ссылка WDTT (легаси)"), ServerLinks::LegacyWdtt(server, password, hashes)));
        layout->addWidget(ShareBlocks::Text(body, QStringLiteral("Пароль"), password, true));
        auto *stats = new QLabel(QStringLiteral("Истекает: %1    Устройства: %2/%3    Трафик: %4")
                                     .arg(dateText(qint64(access["expires_at"].toDouble())))
                                     .arg(access["device_ids"].toArray().size()).arg(access["max_devices"].toInt(1))
                                     .arg(bytesText(access["up_bytes"].toDouble() + access["down_bytes"].toDouble())), body);
        layout->addWidget(stats);
        if (!access["device_ids"].toArray().isEmpty()) {
            auto *group = new QGroupBox(QStringLiteral("Привязанные устройства"), body);
            auto *rows = new QVBoxLayout(group);
            for (const auto &device : access["device_ids"].toArray()) {
                auto *row = new QHBoxLayout;
                auto *name = new QLabel(device.toString(), group);
                name->setTextInteractionFlags(Qt::TextSelectableByMouse);
                row->addWidget(name, 1);
                auto *unbind = new QPushButton(QStringLiteral("Отвязать"), group);
                connect(unbind, &QPushButton::clicked, &dialog, [this, &dialog, password, device] {
                    dialog.accept();
                    admin("/admin/passwords/unbind-device", {{"password", password}, {"device_id", device.toString()}},
                          [this](const QJsonObject &updated) { refresh(); details(updated); });
                });
                row->addWidget(unbind);
                rows->addLayout(row);
            }
            layout->addWidget(group);
        }
        layout->addStretch();
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        auto *edit = buttons->addButton(QStringLiteral("Изменить"), QDialogButtonBox::ActionRole);
        connect(edit, &QPushButton::clicked, &dialog, [this, &dialog, access] { dialog.accept(); form(access); });
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        outer->addWidget(buttons);
        dialog.exec();
    }

public:
    QwdttAccess(QWidget *parent, const QJsonObject &value) : ServerScreen(parent, value, QStringLiteral("Пользователи и подписки")) {
        resize(860, 560);
        auto *layout = new QVBoxLayout(this);
        auto *top = new QHBoxLayout;
        search = new QLineEdit(this);
        search->setPlaceholderText(QStringLiteral("Поиск по имени, паролю или хешу"));
        top->addWidget(search, 1);
        button(QStringLiteral("Обновить"), top, [this] { refresh(); });
        button(QStringLiteral("Новый доступ"), top, [this] { form({}); });
        layout->addLayout(top);
        layout->addWidget(busy);
        view = table(this, {QStringLiteral("Имя"), QStringLiteral("Пароль"), QStringLiteral("Истекает"), QStringLiteral("Устройства"), QStringLiteral("Трафик"), QStringLiteral("Состояние")});
        layout->addWidget(view, 1);
        auto *bottom = new QHBoxLayout;
        button(QStringLiteral("Подробнее"), bottom, [this] { details(selected()); });
        button(QStringLiteral("Изменить"), bottom, [this] { if (const auto o = selected(); !o.isEmpty()) form(o); });
        button(QStringLiteral("Включить / выключить"), bottom, [this] {
            const auto o = selected();
            if (o.isEmpty()) return;
            admin(o["is_deactivated"].toBool() ? "/admin/passwords/activate" : "/admin/passwords/deactivate", {{"password", o["password"]}}, [this](const QJsonObject &) { refresh(); });
        });
        bottom->addStretch();
        button(QStringLiteral("Удалить"), bottom, [this] {
            const auto o = selected();
            if (o.isEmpty() || !confirm(QStringLiteral("Удалить пароль?"), QStringLiteral("Пароль «%1» и все привязанные устройства будут удалены безвозвратно.")
                                                                             .arg(textOr(o["label"], o["password"].toString()))))
                return;
            admin("/admin/passwords/delete", {{"password", o["password"]}}, [this](const QJsonObject &) { refresh(); });
        });
        layout->addLayout(bottom);
        layout->addWidget(status);
        connect(search, &QLineEdit::textChanged, this, [this] { render(); });
        connect(view, &QTableWidget::cellDoubleClicked, this, [this] { details(selected()); });
        refresh();
    }
};

// «Клиенты и сервер» of a WDTT Plus server: its admin interface over SSH, as its app's ServerAdminClient.
class WdttPlusClients : public ServerScreen {
    QLineEdit *search;
    QTableWidget *view;
    QGroupBox *card;
    QLabel *version, *address, *summary;
    QJsonObject info;
    QJsonArray items;

    QJsonObject selected() const {
        const auto row = view->currentRow();
        if (row < 0) return {};
        const auto password = view->item(row, 0)->data(Qt::UserRole).toString();
        for (const auto &entry : items)
            if (entry.toObject()["password"].toString() == password) return entry.toObject();
        return {};
    }

    void refresh() {
        wdttPlus({"list"}, [this](const QJsonObject &value) {
            info = value["server"].toObject();
            items = value["passwords"].toArray();
            render();
        });
    }

    static qint64 traffic(const QJsonObject &o, const QString &period) {
        const auto t = o["traffic"].toObject()[period].toObject();
        if (!t.isEmpty()) return qint64(t["down"].toDouble() + t["up"].toDouble());
        return qint64(o["down_bytes"].toDouble() + o["up_bytes"].toDouble());
    }

    void render() {
        version->setText("WDTT Plus " + textOr(info["wdtt_plus_version"], "—"));
        address->setText(info["effective_public_ip"].toString().isEmpty() ? info["public_ip"].toString() : info["effective_public_ip"].toString());
        summary->setText(QStringLiteral("Клиенты: %1/%2    Устройства: %3    Подключения: %4\nCPU: %5%    Память: %6%    Сеть: ↓%7 ↑%8 Мбит/с")
                             .arg(info["active_password_count"].toInt()).arg(info["max_passwords"].toInt())
                             .arg(info["device_count"].toInt()).arg(info["active_connections"].toInt())
                             .arg(info["cpu_percent"].toDouble(), 0, 'f', 0).arg(info["memory_percent"].toDouble(), 0, 'f', 0)
                             .arg(info["network_rx_mbps"].toDouble(), 0, 'f', 1).arg(info["network_tx_mbps"].toDouble(), 0, 'f', 1));
        const auto query = search->text().trimmed();
        view->setRowCount(0);
        for (const auto &entry : items) {
            const auto o = entry.toObject();
            const auto label = o["label"].toString(), password = o["password"].toString(), state = o["status"].toString();
            if (!query.isEmpty() && !label.contains(query, Qt::CaseInsensitive) && !password.contains(query, Qt::CaseInsensitive) &&
                !o["vk_hash"].toString().contains(query, Qt::CaseInsensitive))
                continue;
            const int row = view->rowCount();
            view->insertRow(row);
            const auto device = textOr(o["device_name"], o["device"].toObject()["name"].toString());
            view->setItem(row, 0, cell(label.isEmpty() ? password : label, password));
            view->setItem(row, 1, cell(password));
            view->setItem(row, 2, cell(dateText(qint64(o["expires_at"].toDouble()))));
            view->setItem(row, 3, cell(!device.isEmpty() ? device : o["device_id"].toString().isEmpty() ? QStringLiteral("нет") : QStringLiteral("привязано")));
            view->setItem(row, 4, cell(bytesText(double(traffic(o, "all")))));
            view->setItem(row, 5, cell(state == "deactivated" ? QStringLiteral("выключен") : state.startsWith("expired") ? QStringLiteral("истёк")
                                       : !o["device_id"].toString().isEmpty() ? QStringLiteral("привязан") : QString()));
        }
        view->resizeColumnsToContents();
        if (items.isEmpty()) status->setText(QStringLiteral("Клиентов пока нет. Создайте первого кнопкой «Новый клиент»."));
    }

    // The client's wdtt://connect link, as its app's buildServerConnectionLink; none without valid ports.
    QString link(const QJsonObject &access) const {
        auto ports = access["ports"].toString();
        if (ports.isEmpty()) ports = textOr(info["default_ports"], "56000,56001,9000");
        const auto parts = ports.split(',');
        QList<int> numbers;
        for (const auto &part : parts) {
            bool ok;
            const int n = part.trimmed().toInt(&ok);
            if (!ok || n < 1 || n > 65535) return {};
            numbers.append(n);
        }
        if (numbers.size() != 3) return {};
        Configs::wdttplus profile;
        for (const auto &candidate : {info["effective_public_ip"].toString(), info["public_ip"].toString(), server["host"].toString()})
            if (profile.server.isEmpty() && !candidate.trimmed().isEmpty()) profile.server = candidate.trimmed();
        profile.server_port = numbers[0];
        profile.wgPort = numbers[1];
        profile.localPort = numbers[2];
        profile.password = access["password"].toString();
        profile.hashes = hashList(access["vk_hash"].toString());
        profile.name = textOr(access["label"], profile.password);
        return profile.ExportToLink();
    }

    void createForm() {
        FormDialog dialog(this, QStringLiteral("Новый клиент"));
        auto *label = new QLineEdit(&dialog.dialog);
        dialog.form->addRow(QStringLiteral("Имя (кому выдан)"), label);
        auto *hashes = new HashFields(&dialog.dialog, {});
        dialog.form->addRow(QStringLiteral("VK хеши"), hashes);
        auto *days = spin(&dialog.dialog, 0, 365, 30);
        days->setSpecialValueText(QStringLiteral("бессрочно"));
        dialog.form->addRow(QStringLiteral("Дней"), days);
        auto *password = new QLineEdit(&dialog.dialog);
        password->setPlaceholderText(QStringLiteral("пусто — сгенерировать"));
        dialog.form->addRow(QStringLiteral("Пароль"), password);
        if (!dialog.exec([&] {
                const auto own = password->text().trimmed();
                if (!own.isEmpty() && (own.size() < 8 || own.size() > 64)) {
                    QMessageBox::warning(&dialog.dialog, windowTitle(), QStringLiteral("Пароль: от 8 до 64 символов"));
                    return false;
                }
                return true;
            }))
            return;
        QStringList args{"create", "--days", QString::number(days->value())};
        if (!label->text().trimmed().isEmpty()) args += {"--label", label->text().trimmed()};
        if (!hashes->hashes().isEmpty()) args += {"--vk-hash", hashes->hashes().join(',')};
        if (!password->text().trimmed().isEmpty()) args += {"--client-password", password->text().trimmed()};
        wdttPlus(args, [this](const QJsonObject &) { refresh(); });
    }

    // The client with its link, QR code, password, traffic, device and every edit WDTT Plus offers.
    void details(const QJsonObject &access) {
        if (access.isEmpty()) return;
        const auto password = access["password"].toString();
        QDialog dialog(this);
        dialog.setWindowTitle(textOr(access["label"], password));
        dialog.resize(660, 820);
        auto *outer = new QVBoxLayout(&dialog);
        auto *scroll = new QScrollArea(&dialog);
        scroll->setWidgetResizable(true);
        outer->addWidget(scroll);
        auto *body = new QWidget(scroll);
        scroll->setWidget(body);
        auto *layout = new QVBoxLayout(body);
        const auto share = link(access);
        if (share.isEmpty()) layout->addWidget(new QLabel(QStringLiteral("У клиента некорректные порты: ссылку не собрать"), body));
        else {
            layout->addWidget(ShareBlocks::Text(body, QStringLiteral("Ссылка WDTT Plus"), share));
            layout->addWidget(ShareBlocks::Qr(body, QStringLiteral("QR-код"), share));
        }
        layout->addWidget(ShareBlocks::Text(body, QStringLiteral("Пароль"), password, true));
        layout->addWidget(new QLabel(QStringLiteral("Истекает: %1    Сегодня: %2    Месяц: %3")
                                         .arg(dateText(qint64(access["expires_at"].toDouble())))
                                         .arg(bytesText(double(traffic(access, "today")))).arg(bytesText(double(traffic(access, "month")))), body));
        if (!access["device_id"].toString().isEmpty()) {
            auto *group = new QGroupBox(QStringLiteral("Устройство"), body);
            auto *row = new QHBoxLayout(group);
            QStringList parts;
            for (const auto &key : {"device_name", "device_ip"})
                if (!access[key].toString().isEmpty()) parts.append(access[key].toString());
            row->addWidget(new QLabel(parts.isEmpty() ? access["device_id"].toString() : parts.join(" · "), group), 1);
            auto *unbind = new QPushButton(QStringLiteral("Отвязать устройство"), group);
            connect(unbind, &QPushButton::clicked, &dialog, [this, &dialog, password] {
                dialog.accept();
                wdttPlus({"unbind", "--password", password}, [this](const QJsonObject &) { refresh(); });
            });
            row->addWidget(unbind);
            layout->addWidget(group);
        }
        auto *editGroup = new QGroupBox(QStringLiteral("Изменить"), body);
        auto *form = new QFormLayout(editGroup);
        auto *label = new QLineEdit(access["label"].toString(), editGroup);
        form->addRow(QStringLiteral("Имя"), label);
        auto *hashes = new HashFields(editGroup, hashList(access["vk_hash"].toString()));
        form->addRow(QStringLiteral("VK хеши"), hashes);
        auto *days = new QLineEdit(editGroup);
        days->setPlaceholderText(QStringLiteral("пусто — без изменений, 0 — бессрочно"));
        form->addRow(QStringLiteral("Новый срок, дней"), days);
        auto *newPassword = new QLineEdit(editGroup);
        newPassword->setPlaceholderText(QStringLiteral("пусто — без изменений"));
        form->addRow(QStringLiteral("Новый пароль"), newPassword);
        auto *save = new QPushButton(QStringLiteral("Сохранить"), editGroup);
        form->addRow(save);
        layout->addWidget(editGroup);
        layout->addStretch();
        connect(save, &QPushButton::clicked, &dialog, [&, this] {
            bool daysOK = true;
            const int dayCount = days->text().trimmed().isEmpty() ? -1 : days->text().trimmed().toInt(&daysOK);
            if (!daysOK || dayCount > 365) { QMessageBox::warning(&dialog, windowTitle(), QStringLiteral("Дней: от 0 до 365")); return; }
            const auto own = newPassword->text().trimmed();
            if (!own.isEmpty() && (own.size() < 8 || own.size() > 64)) { QMessageBox::warning(&dialog, windowTitle(), QStringLiteral("Пароль: от 8 до 64 символов")); return; }
            // Each change is its own admin command, applied one after another; the new password last.
            if (label->text().trimmed() != access["label"].toString()) wdttPlus({"set-label", "--password", password, "--label", label->text().trimmed()}, [](const QJsonObject &) {});
            if (hashes->hashes() != hashList(access["vk_hash"].toString())) wdttPlus({"set-hash", "--password", password, "--vk-hash", hashes->hashes().join(',')}, [](const QJsonObject &) {});
            if (dayCount >= 0) wdttPlus({"set-expiry", "--password", password, "--days", QString::number(dayCount)}, [](const QJsonObject &) {});
            if (!own.isEmpty()) wdttPlus({"set-password", "--password", password, "--new-password", own}, [](const QJsonObject &) {});
            dialog.accept();
            refresh();
        });
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        outer->addWidget(buttons);
        dialog.exec();
    }

    void settingsForm() {
        FormDialog dialog(this, QStringLiteral("Настройки сервера"));
        auto *dns = new QLineEdit(info["dns"].toString(), &dialog.dialog);
        dialog.form->addRow(QStringLiteral("DNS для клиентов"), dns);
        auto *limit = spin(&dialog.dialog, 1, 500, info["max_passwords"].toInt(50));
        dialog.form->addRow(QStringLiteral("Лимит доступов"), limit);
        auto *ports = new QLineEdit(info["default_ports"].toString(), &dialog.dialog);
        dialog.form->addRow(QStringLiteral("Порты по умолчанию (DTLS,WG,локальный)"), ports);
        auto *host = new QLineEdit(info["public_ip"].toString(), &dialog.dialog);
        host->setPlaceholderText(QStringLiteral("пусто — авто"));
        dialog.form->addRow(QStringLiteral("Публичный адрес"), host);
        if (!dialog.exec()) return;
        wdttPlus({"update-settings", "--dns", dns->text().trimmed(), "--limit", QString::number(limit->value()), "--ports", ports->text().trimmed(),
                  "--public-ip", host->text().trimmed().isEmpty() ? QStringLiteral("auto") : host->text().trimmed()},
                 [this](const QJsonObject &) { refresh(); });
    }

    void maintenance(QPushButton *anchor) {
        QMenu menu(this);
        for (const auto &[command, label] : QList<QPair<QString, QString>>{
                 {"refresh-public-ip", "Определить публичный адрес заново"}, {"cleanup-expired", "Удалить истёкших клиентов"},
                 {"cleanup-orphans", "Отвязать сиротские устройства"}, {"reset-traffic", "Сбросить статистику трафика"}, {"restart", "Перезапустить сервер"}}) {
            menu.addAction(label, this, [this, command, label] {
                if (confirm(QStringLiteral("Обслуживание"), label + "?")) wdttPlus({command}, [this](const QJsonObject &) { refresh(); });
            });
        }
        menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
    }

public:
    WdttPlusClients(QWidget *parent, const QJsonObject &value) : ServerScreen(parent, value, QStringLiteral("Клиенты и сервер")) {
        resize(900, 660);
        auto *layout = new QVBoxLayout(this);
        card = new QGroupBox(QStringLiteral("Сервер"), this);
        auto *cardLayout = new QGridLayout(card);
        version = new QLabel(card);
        version->setStyleSheet("font-weight: bold");
        address = new QLabel(card);
        address->setTextInteractionFlags(Qt::TextSelectableByMouse);
        summary = new QLabel(card);
        cardLayout->addWidget(version, 0, 0);
        cardLayout->addWidget(address, 0, 1);
        cardLayout->addWidget(summary, 1, 0, 1, 2);
        auto *cardButtons = new QHBoxLayout;
        button(QStringLiteral("Настройки"), cardButtons, [this] { settingsForm(); });
        auto *service = button(QStringLiteral("Обслуживание"), cardButtons, {});
        connect(service, &QPushButton::clicked, this, [this, service] { maintenance(service); });
        cardButtons->addStretch();
        cardLayout->addLayout(cardButtons, 2, 0, 1, 2);
        layout->addWidget(card);
        auto *top = new QHBoxLayout;
        search = new QLineEdit(this);
        search->setPlaceholderText(QStringLiteral("Поиск по имени, паролю или хешу"));
        top->addWidget(search, 1);
        button(QStringLiteral("Обновить"), top, [this] { refresh(); });
        button(QStringLiteral("Новый клиент"), top, [this] { createForm(); });
        layout->addLayout(top);
        layout->addWidget(busy);
        view = table(this, {QStringLiteral("Имя"), QStringLiteral("Пароль"), QStringLiteral("Истекает"), QStringLiteral("Устройство"), QStringLiteral("Трафик"), QStringLiteral("Состояние")});
        layout->addWidget(view, 1);
        auto *bottom = new QHBoxLayout;
        button(QStringLiteral("Подробнее и изменение"), bottom, [this] { details(selected()); });
        button(QStringLiteral("Включить / выключить"), bottom, [this] {
            const auto o = selected();
            if (o.isEmpty()) return;
            wdttPlus({o["status"].toString() == "deactivated" ? "activate" : "deactivate", "--password", o["password"].toString()}, [this](const QJsonObject &) { refresh(); });
        });
        bottom->addStretch();
        button(QStringLiteral("Удалить"), bottom, [this] {
            const auto o = selected();
            if (o.isEmpty() || !confirm(QStringLiteral("Удалить клиента?"), QStringLiteral("Доступ «%1» и привязка устройства будут удалены.").arg(textOr(o["label"], o["password"].toString()))))
                return;
            wdttPlus({"delete", "--password", o["password"].toString()}, [this](const QJsonObject &) { refresh(); });
        });
        layout->addLayout(bottom);
        layout->addWidget(status);
        connect(search, &QLineEdit::textChanged, this, [this] { render(); });
        connect(view, &QTableWidget::cellDoubleClicked, this, [this] { details(selected()); });
        refresh();
    }
};

// «Резервные копии и перенос»: the server's own backups, its schedule and a portable encrypted copy.
class WdttPlusBackups : public ServerScreen {
    QLabel *policy, *stats, *lastError;
    QTableWidget *view;
    QJsonObject info;

    void apply(const QJsonObject &value) {
        info = value;
        const auto p = info["policy"].toObject(), runtime = info["state"].toObject();
        policy->setText(p["enabled"].toBool() ? QStringLiteral("Автоматически каждые %1 ч, хранится %2").arg(p["interval_hours"].toInt(24)).arg(p["retention_count"].toInt(14))
                                              : QStringLiteral("Создаются только вручную и перед опасными операциями"));
        stats->setText(QStringLiteral("Последняя: %1    Следующая: %2    Объём: %3")
                           .arg(timeText(qint64(runtime["last_success_at"].toDouble()))).arg(timeText(qint64(info["next_run_at"].toDouble())))
                           .arg(bytesText(info["total_bytes"].toDouble())));
        lastError->setText(runtime["last_error"].toString().isEmpty() ? QString() : QStringLiteral("Ошибка: ") + runtime["last_error"].toString());
        view->setRowCount(0);
        for (const auto &entry : info["backups"].toArray()) {
            const auto b = entry.toObject();
            const int row = view->rowCount();
            view->insertRow(row);
            view->setItem(row, 0, cell(timeText(qint64(b["created_at"].toDouble())), b["id"].toString()));
            view->setItem(row, 1, cell(b["reason"].toString()));
            view->setItem(row, 2, cell(b["server_version"].toString()));
            view->setItem(row, 3, cell(bytesText(b["size_bytes"].toDouble())));
            view->setItem(row, 4, cell(b["valid"].toBool() ? QStringLiteral("в порядке") : QStringLiteral("повреждена: ") + b["error"].toString()));
        }
        view->resizeColumnsToContents();
        if (info["backups"].toArray().isEmpty()) status->setText(QStringLiteral("Копий пока нет"));
    }

    QString selectedID() const {
        const auto row = view->currentRow();
        return row < 0 ? QString() : view->item(row, 0)->data(Qt::UserRole).toString();
    }

    void refresh() { wdttPlus({"backup-status"}, [this](const QJsonObject &v) { apply(v["backup_status"].toObject()); }, 120); }

    void schedule() {
        const auto p = info["policy"].toObject();
        FormDialog dialog(this, QStringLiteral("Автоматические копии"));
        auto *enabled = new QCheckBox(QStringLiteral("Включены"), &dialog.dialog);
        enabled->setChecked(p["enabled"].toBool());
        dialog.form->addRow(enabled);
        auto *interval = spin(&dialog.dialog, 6, 168, p["interval_hours"].toInt(24));
        dialog.form->addRow(QStringLiteral("Интервал, ч"), interval);
        auto *retention = spin(&dialog.dialog, 2, 90, p["retention_count"].toInt(14));
        dialog.form->addRow(QStringLiteral("Количество копий"), retention);
        if (!dialog.exec()) return;
        wdttPlus({"backup-configure", "--enabled", enabled->isChecked() ? "true" : "false", "--interval-hours", QString::number(interval->value()),
                  "--retention", QString::number(retention->value())},
                 [this](const QJsonObject &v) { apply(v["backup_status"].toObject()); }, 120);
    }

    // As WDTT Plus downloads a copy: its portable backup, encrypted with a file password of 8+ characters.
    void exportBackup(const QString &id) {
        if (id.isEmpty()) return;
        bool ok = false;
        const auto password = QInputDialog::getText(this, QStringLiteral("Скачать копию"), QStringLiteral("Пароль файла (не меньше 8 символов)"), QLineEdit::Password, {}, &ok);
        if (!ok) return;
        if (password.size() < 8) { QMessageBox::warning(this, windowTitle(), QStringLiteral("Пароль должен содержать не меньше 8 символов")); return; }
        call("wdttplus-backup-export", {{"id", id}, {"password", password}}, [this, id](const QJsonValue &value) {
            const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Сохранить копию"),
                                                           QString("wdtt-plus-backup-%1-%2.json").arg(server["host"].toString(), id), "JSON (*.json)");
            if (path.isEmpty()) return;
            QFile file(path);
            const auto data = value.toString().toUtf8();
            if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) status->setText(QStringLiteral("Не удалось сохранить файл"));
            else status->setText(QStringLiteral("Копия сохранена: ") + path);
        });
    }

public:
    WdttPlusBackups(QWidget *parent, const QJsonObject &value) : ServerScreen(parent, value, QStringLiteral("Резервные копии и перенос")) {
        resize(820, 560);
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel(QStringLiteral("Копии хранятся на VPS, расписание работает независимо от программы."), this));
        auto *group = new QGroupBox(QStringLiteral("Автоматические копии"), this);
        auto *groupLayout = new QVBoxLayout(group);
        policy = new QLabel(group);
        stats = new QLabel(group);
        lastError = new QLabel(group);
        lastError->setStyleSheet("color: #e87870");
        groupLayout->addWidget(policy);
        groupLayout->addWidget(stats);
        groupLayout->addWidget(lastError);
        auto *row = new QHBoxLayout;
        button(QStringLiteral("Расписание"), row, [this] { schedule(); });
        button(QStringLiteral("Создать копию"), row, [this] { wdttPlus({"backup-create", "--reason", "manual"}, [this](const QJsonObject &v) { apply(v["backup_status"].toObject()); }, 180); });
        button(QStringLiteral("Обновить"), row, [this] { refresh(); });
        row->addStretch();
        groupLayout->addLayout(row);
        layout->addWidget(group);
        layout->addWidget(busy);
        view = table(this, {QStringLiteral("Создана"), QStringLiteral("Причина"), QStringLiteral("Версия сервера"), QStringLiteral("Размер"), QStringLiteral("Проверка")});
        layout->addWidget(view, 1);
        auto *bottom = new QHBoxLayout;
        button(QStringLiteral("Проверить"), bottom, [this] {
            if (const auto id = selectedID(); !id.isEmpty()) wdttPlus({"backup-verify", "--id", id}, [this](const QJsonObject &) { refresh(); }, 120);
        });
        button(QStringLiteral("Скачать…"), bottom, [this] { exportBackup(selectedID()); });
        bottom->addStretch();
        button(QStringLiteral("Удалить"), bottom, [this] {
            const auto id = selectedID();
            if (id.isEmpty() || !confirm(QStringLiteral("Удалить копию"), QStringLiteral("Удалить копию %1?").arg(view->item(view->currentRow(), 0)->text()))) return;
            wdttPlus({"backup-delete", "--id", id, "--confirm", QStringLiteral("УДАЛИТЬ")}, [this](const QJsonObject &v) { apply(v["backup_status"].toObject()); }, 120);
        });
        layout->addLayout(bottom);
        layout->addWidget(status);
        refresh();
    }
};

bool installed(const QJsonObject &server) { return !server["config"].toString().isEmpty(); }
}

void OpenServerMenu(QWidget *parent, const std::function<QJsonObject()> &current, const std::function<void()> &editor) {
    const auto server = current();
    const auto kind = server["kind"].toString();
    auto *menu = new QDialog(parent);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setWindowTitle(textOr(server["name"], server["host"].toString()));
    menu->resize(560, 0);
    auto *layout = new QVBoxLayout(menu);
    auto *header = new QLabel(QStringLiteral("<b>%1</b><br>%2 · SSH %3 · %4")
                                  .arg(server["name"].toString().toHtmlEscaped(), server["host"].toString().toHtmlEscaped(), server["sshPort"].toString(),
                                       installed(server) ? QStringLiteral("установлен") : QStringLiteral("не установлен")),
                              menu);
    layout->addWidget(header);
    const auto entry = [&](const QString &title, const QString &description, const std::function<void()> &open) {
        auto *item = new QCommandLinkButton(title, description, menu);
        QObject::connect(item, &QCommandLinkButton::clicked, menu, open);
        layout->addWidget(item);
    };
    const auto needsInstall = [menu](bool available, const std::function<void()> &open) {
        if (available) open();
        else QMessageBox::information(menu, menu->windowTitle(), QStringLiteral("Доступно после установки сервера из программы."));
    };
    if (kind == "qwdtt") {
        entry(QStringLiteral("Пользователи и подписки"), QStringLiteral("Доступы, лимиты устройств и сроки действия"), [=] {
            const auto s = current();
            needsInstall(installed(s) && s["adminPin"].toString().startsWith("sha256/") && !s["adminToken"].toString().isEmpty(),
                         [=] { (new QwdttAccess(parent, s))->show(); });
        });
    } else if (kind == "csqtt") {
        entry(QStringLiteral("Панель сервера"), QStringLiteral("Пароли, устройства и звонки в веб-панели CSQTT"), [=] {
            const auto s = current();
            QUrl url;
            url.setScheme("https");
            url.setHost(s["host"].toString());
            url.setPort(s["webPort"].toString().toInt());
            QDesktopServices::openUrl(url);
        });
    } else if (kind == "wdttplus") {
        const auto available = [](const QJsonObject &s) { return installed(s) && !s["hostKey"].toString().isEmpty() && !s["password"].toString().isEmpty(); };
        entry(QStringLiteral("Клиенты и сервер"), QStringLiteral("Пароли, хеши, устройства, сроки и настройки сервера"), [=] {
            const auto s = current();
            needsInstall(available(s), [=] { (new WdttPlusClients(parent, s))->show(); });
        });
        entry(QStringLiteral("Выходной IP и прокси"), QStringLiteral("Прокси, WARP, WireGuard или второй сервер для выхода"), [=] {
            const auto s = current();
            needsInstall(available(s), [=] { OpenWdttPlusExit(parent, s); });
        });
        entry(QStringLiteral("Резервные копии и перенос"), QStringLiteral("Расписание, проверка и выгрузка копий"), [=] {
            const auto s = current();
            needsInstall(available(s), [=] { (new WdttPlusBackups(parent, s))->show(); });
        });
    }
    entry(QStringLiteral("Управление сервером"), QStringLiteral("SSH, порты, обновление, переустановка, удаление и консоль деплоя"), [menu, editor] {
        menu->accept();
        editor();
    });
    menu->show();
}
