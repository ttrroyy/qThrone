#include "include/ui/setting/dialog_servers.h"
#include "include/configs/outbounds/mrtt.h"
#include "include/configs/outbounds/qwdtt.h"
#include "include/configs/outbounds/wdttplus.h"
#include "include/global/Utils.hpp"
#include "include/ui/setting/dialog_vk_relay.h"
#include "include/ui/setting/dialog_wdttplus_exit.h"
#include "include/ui/utils/ShareBlocks.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QRandomGenerator>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
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
#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
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

// A click on the empty part of a table drops its selection and current row.
class ClearOnEmptyClick : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *view = static_cast<QTableWidget *>(parent());
            if (!view->indexAt(static_cast<QMouseEvent *>(event)->position().toPoint()).isValid()) {
                view->clearSelection();
                view->setCurrentIndex({});
            }
        }
        return false;
    }
};

QTableWidget *table(QWidget *parent, const QStringList &headers) {
    auto *view = new QTableWidget(0, int(headers.size()), parent);
    view->setHorizontalHeaderLabels(headers);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->verticalHeader()->setVisible(false);
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    view->setAlternatingRowColors(true);
    view->viewport()->installEventFilter(new ClearOnEmptyClick(view));
    return view;
}

// Up to four hash fields; a pasted VK call link is read as its hash.
class HashFields : public QWidget {
public:
    QList<QLineEdit *> fields;
    HashFields(QWidget *parent, const QStringList &initial, int count = 4) : QWidget(parent), limit(count) {
        auto *column = new QVBoxLayout(this);
        column->setContentsMargins(0, 0, 0, 0);
        for (int i = 0; i < limit; ++i) {
            auto *field = new QLineEdit(i < initial.size() ? initial[i] : QString(), this);
            field->setPlaceholderText(QStringLiteral("VK хеш или ссылка на звонок"));
            fields.append(field);
            column->addWidget(field);
        }
    }
    QStringList hashes() const {
        QStringList values;
        for (auto *field : fields) values.append(field->text());
        return Configs::qwdtt::NormalizeHashes(values).mid(0, limit);
    }
private:
    int limit;
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
            admin("/admin/passwords", values, [this](const QJsonObject &) { refresh(); });
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

// The clients of a deployed FreeTurn server, as its app lists them: each has its own link and WireGuard peer.
class FreeturnClients : public ServerScreen {
    QListWidget *list;

    void request(const QString &action, const QString &name, const std::function<void(const QJsonObject &)> &done) {
        call("freeturn-admin", {{"action", action}, {"name", name}}, [done](const QJsonValue &value) { done(value.toObject()); });
    }
    void reload() {
        request("list", {}, [this](const QJsonObject &value) {
            list->clear();
            for (const auto &entry : value["clients"].toArray()) {
                const auto client = entry.toObject();
                auto *item = new QListWidgetItem(client["name"].toString() + (client["ip"].toString().isEmpty() ? QString() : " · " + client["ip"].toString()), list);
                item->setData(Qt::UserRole, client["name"].toString());
                item->setData(Qt::UserRole + 1, client["self"].toBool());
            }
        });
    }
    void open(const QString &name, bool owner) {
        request("show", name, [this, name, owner](const QJsonObject &value) {
            QMessageBox box(this);
            box.setWindowTitle(name);
            box.setText(value["link"].toString());
            box.setTextInteractionFlags(Qt::TextSelectableByMouse);
            auto *copy = box.addButton(QStringLiteral("Копировать ссылку"), QMessageBox::ActionRole);
            auto *remove = owner ? nullptr : box.addButton(QStringLiteral("Удалить"), QMessageBox::DestructiveRole);
            box.addButton(QMessageBox::Close);
            box.exec();
            if (box.clickedButton() == copy) QApplication::clipboard()->setText(value["link"].toString());
            if (remove && box.clickedButton() == remove) request("remove", name, [this](const QJsonObject &) { reload(); });
        });
    }
public:
    FreeturnClients(QWidget *parent, const QJsonObject &value) : ServerScreen(parent, value, QStringLiteral("Клиенты FreeTurn")) {
        resize(460, 420);
        auto *body = new QVBoxLayout(this);
        list = new QListWidget(this);
        auto *add = new QPushButton(QStringLiteral("Добавить клиента"), this);
        actions.append(add);
        body->addWidget(add);
        body->addWidget(busy);
        body->addWidget(status);
        body->addWidget(list);
        connect(list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) { open(item->data(Qt::UserRole).toString(), item->data(Qt::UserRole + 1).toBool()); });
        connect(add, &QPushButton::clicked, this, [this] {
            const auto name = QInputDialog::getText(this, windowTitle(), QStringLiteral("Имя клиента")).trimmed();
            if (!name.isEmpty()) request("add", name, [this](const QJsonObject &) { reload(); });
        });
        reload();
    }
};

// A deployed MRTT server, as its own app manages it: the API listens on 127.0.0.1 and is reached through SSH.
class MrttServer : public ServerScreen {
    QTabWidget *tabs;
    QTableWidget *clientTable;
    QLineEdit *search;
    QComboBox *groupFilter;
    QTableWidget *groupTable, *ruleTable, *proxyTable;
    QLabel *proxyState;
    QPlainTextEdit *logView;
    QCheckBox *logging;
    QFormLayout *statsForm;
    QJsonArray clients, groups, rules, proxies;
    QJsonObject settings;
    QTimer *liveTimer;

    static QString enc(const QString &value) { return QString::fromLatin1(QUrl::toPercentEncoding(value)); }
    static QString compact(const QJsonObject &value) { return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)); }
    void api(const QString &method, const QString &path, const QJsonObject &body, const std::function<void(const QString &)> &done) {
        call("mrtt-admin", {{"method", method}, {"path", path}, {"body", method == "GET" || method == "DELETE" ? QString() : compact(body)}},
             [done](const QJsonValue &value) { done(value.toString()); });
    }
    void changed(const QString &method, const QString &path, const QJsonObject &body, const QString &message) {
        api(method, path, body, [this, message](const QString &) { reload(); status->setText(message); });
    }
    static bool isOwner(const QJsonObject &client) {
        return client["ip"].toString() == "10.66.67.2" && client["expires"].toDouble() == 0 && client["group_id"].toString().isEmpty() && client["name"].toString() == QStringLiteral("Владелец");
    }
    QString groupName(const QString &id) const {
        for (const auto &entry : groups) if (entry.toObject()["id"].toString() == id) return entry.toObject()["name"].toString();
        return {};
    }
    void reload() {
        api("GET", "/api/groups", {}, [this](const QString &text) {
            groups = QJsonDocument::fromJson(text.toUtf8()).array();
            const auto current = groupFilter->currentData();
            const QSignalBlocker blocker(groupFilter);
            groupFilter->clear();
            groupFilter->addItem(QStringLiteral("Все"), QVariant());
            groupFilter->addItem(QStringLiteral("Без группы"), QString(""));
            for (const auto &entry : groups) groupFilter->addItem(entry.toObject()["name"].toString(), entry.toObject()["id"].toString());
            groupFilter->setCurrentIndex(qMax(0, groupFilter->findData(current)));
            showGroups();
            api("GET", "/api/clients", {}, [this](const QString &list) {
                clients = QJsonDocument::fromJson(list.toUtf8()).array();
                showClients();
            });
        });
        api("GET", "/api/routing-rules", {}, [this](const QString &text) { rules = QJsonDocument::fromJson(text.toUtf8()).array(); showRules(); showGroups(); });
        api("GET", "/api/settings", {}, [this](const QString &text) { settings = QJsonDocument::fromJson(text.toUtf8()).object(); });
        api("GET", "/api/local-proxy", {}, [this](const QString &text) { showProxy(QJsonDocument::fromJson(text.toUtf8()).object()); });
    }
    void showClients() {
        QList<QJsonObject> visible;
        for (const auto &entry : clients) {
            const auto client = entry.toObject();
            if (groupFilter->currentData().isValid() && client["group_id"].toString() != groupFilter->currentData().toString()) continue;
            if (!search->text().isEmpty() && !client["name"].toString().contains(search->text(), Qt::CaseInsensitive)) continue;
            visible.append(client);
        }
        std::sort(visible.begin(), visible.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return isOwner(a) != isOwner(b) ? isOwner(a) : a["name"].toString().toLower() < b["name"].toString().toLower();
        });
        clientTable->setRowCount(0);
        for (const auto &client : visible) {
            const int row = clientTable->rowCount();
            clientTable->insertRow(row);
            const qint64 expires = qint64(client["expires"].toDouble());
            const auto state = isOwner(client) ? QStringLiteral("владелец")
                : !client["active"].toBool() && expires != 0 && expires < QDateTime::currentSecsSinceEpoch() ? QStringLiteral("истекла")
                : !client["active"].toBool() ? QStringLiteral("выключена") : QStringLiteral("активна");
            int online = 0;
            const auto devices = client["devices"].toArray();
            for (const auto &device : devices) if (device.toObject()["online"].toBool()) online++;
            const int limit = client["max_devices"].toInt(1);
            clientTable->setItem(row, 0, cell(client["name"].toString(), client["password"].toString()));
            clientTable->setItem(row, 1, cell(state));
            clientTable->setItem(row, 2, cell(groupName(client["group_id"].toString())));
            clientTable->setItem(row, 3, cell(expires == 0 ? QStringLiteral("бессрочно") : QStringLiteral("до ") + dateText(expires)));
            clientTable->setItem(row, 4, cell(QStringLiteral("%1/%2 (в сети %3)").arg(devices.size()).arg(limit == 0 ? QStringLiteral("∞") : QString::number(limit)).arg(online)));
            clientTable->setItem(row, 5, cell(QStringLiteral("↑ %1 ↓ %2").arg(bytesText(client["up"].toDouble()), bytesText(client["down"].toDouble()))));
        }
    }
    void showGroups() {
        groupTable->setRowCount(0);
        for (const auto &entry : groups) {
            const auto group = entry.toObject();
            const int row = groupTable->rowCount();
            groupTable->insertRow(row);
            groupTable->setItem(row, 0, cell(group["name"].toString(), group["id"].toString()));
            groupTable->setItem(row, 1, cell(QString::number(group["member_count"].toInt())));
            groupTable->setItem(row, 2, cell(QString::number(group["active_sessions"].toInt())));
            groupTable->setItem(row, 3, cell(QStringLiteral("↑ %1 ↓ %2").arg(bytesText(group["up"].toDouble()), bytesText(group["down"].toDouble()))));
            QString route;
            for (const auto &rule : rules) if (rule.toObject()["id"] == group["routing_rule_id"]) route = rule.toObject()["name"].toString();
            groupTable->setItem(row, 4, cell(route));
        }
    }
    void showRules() {
        ruleTable->setRowCount(0);
        for (const auto &entry : rules) {
            const auto rule = entry.toObject();
            const int row = ruleTable->rowCount();
            ruleTable->insertRow(row);
            ruleTable->setItem(row, 0, cell(rule["name"].toString(), rule["id"].toString()));
            ruleTable->setItem(row, 1, cell(QStringLiteral("Приложений: %1 · доменов: %2%3 · IP: %4 · групп: %5 · подписок: %6")
                .arg(rule["packages"].toArray().size()).arg(rule["direct_domains"].toArray().size())
                .arg(rule["template_domains"].toBool() ? QStringLiteral(" + шаблон") : QString())
                .arg(rule["ips"].toArray().size()).arg(rule["assigned_groups"].toArray().size()).arg(rule["assigned_clients"].toArray().size())));
        }
    }
    void showProxy(const QJsonObject &state) {
        const auto active = state["active_profile_id"].toString();
        proxies = state["profiles"].toArray();
        QString text = active.isEmpty() ? QStringLiteral("Маршрут через прокси выключен") : QStringLiteral("Маршрут через прокси включён. ") +
            (state["route_active"].toBool() ? QStringLiteral("Маршрут применён") : QStringLiteral("Маршрут применяется..."));
        if (!active.isEmpty() && !state["port_listening"].toBool(true)) text += QStringLiteral("\nПорт прокси не слушает");
        if (state["health_error"].isString() && !state["health_error"].toString().isEmpty()) text += "\n" + state["health_error"].toString();
        proxyState->setText(text);
        proxyTable->setRowCount(0);
        for (const auto &entry : proxies) {
            const auto profile = entry.toObject();
            const int row = proxyTable->rowCount();
            proxyTable->insertRow(row);
            proxyTable->setItem(row, 0, cell(profile["name"].toString() + (profile["id"].toString() == active ? QStringLiteral(" · активен") : QString()), profile["id"].toString()));
            proxyTable->setItem(row, 1, cell(QStringLiteral("127.0.0.1:%1").arg(profile["port"].toInt(45000)) + (profile["username"].toString().isEmpty() ? QString() : QStringLiteral(" · с авторизацией"))));
        }
    }
    QStringList selectedPasswords() const {
        QStringList values;
        for (auto *item : clientTable->selectedItems())
            if (item->column() == 0 && !item->data(Qt::UserRole).toString().isEmpty()) {
                for (const auto &entry : clients) {
                    const auto client = entry.toObject();
                    if (client["password"].toString() == item->data(Qt::UserRole).toString() && !isOwner(client)) values << client["password"].toString();
                }
            }
        return values;
    }
    void batch(const QString &action, QJsonObject body, const QString &message) {
        const auto chosen = selectedPasswords();
        if (chosen.isEmpty()) { status->setText(QStringLiteral("Выберите конфиги")); return; }
        body["passwords"] = QJsonArray::fromStringList(chosen);
        changed("POST", "/api/clients/batch-" + action, body, message);
    }
    void shareLink(const QString &title, const QString &link) {
        QDialog dialog(this);
        dialog.setWindowTitle(title);
        auto *layout = new QVBoxLayout(&dialog);
        layout->addWidget(ShareBlocks::Qr(&dialog, QStringLiteral("QR-код"), link));
        layout->addWidget(ShareBlocks::Text(&dialog, QStringLiteral("Ссылка"), link));
        dialog.exec();
    }
    void editClient(const QJsonObject &client) {
        const bool creating = client.isEmpty(), owner = !creating && isOwner(client);
        FormDialog form(this, creating ? QStringLiteral("Новый конфиг") : client["name"].toString());
        auto *name = new QLineEdit(client["name"].toString(), &form.dialog);
        auto *group = new QComboBox(&form.dialog);
        group->addItem(QStringLiteral("Без группы"), QString());
        for (const auto &entry : groups) group->addItem(entry.toObject()["name"].toString(), entry.toObject()["id"].toString());
        group->setCurrentIndex(qMax(0, group->findData(client["group_id"].toString())));
        const qint64 expires = qint64(client["expires"].toDouble());
        int remaining = creating ? 30 : expires == 0 ? 0 : int(qMax<qint64>(0, expires - QDateTime::currentSecsSinceEpoch()) / 86400 + 1);
        auto *days = spin(&form.dialog, 0, 3650, remaining);
        if (!owner) {
            form.form->addRow(QStringLiteral("Имя"), name);
            form.form->addRow(QStringLiteral("Группа"), group);
            form.form->addRow(QStringLiteral("Срок, дней (0 — бессрочно)"), days);
        }
        auto *workers = spin(&form.dialog, 0, 100, client["workers"].toInt());
        form.form->addRow(QStringLiteral("Потоки (0 — как в приложении, 3–100)"), workers);
        auto *devices = spin(&form.dialog, 0, 1000, client["max_devices"].toInt(creating ? 0 : 1));
        form.form->addRow(QStringLiteral("Устройств (0 — без лимита)"), devices);
        auto *transport = new QComboBox(&form.dialog);
        transport->addItem(QStringLiteral("Не задан"), QString()); transport->addItem("VK", "vk"); transport->addItem("Jitsi", "jitsi");
        transport->setCurrentIndex(qMax(0, transport->findData(client["transport"].toString())));
        form.form->addRow(QStringLiteral("Транспорт в ссылке"), transport);
        auto *exit = new QComboBox(&form.dialog);
        const auto exits = settings["exits"].toArray();
        const bool configDecides = settings["exit_priority"].toString() == "config";
        exit->addItem(QStringLiteral("Как на сервере"), QString()); exit->addItem(QStringLiteral("Этот сервер"), "direct");
        for (const auto &entry : exits) exit->addItem(entry.toObject()["name"].toString(), entry.toObject()["id"].toString());
        exit->setCurrentIndex(qMax(0, exit->findData(client["exit"].toString())));
        exit->setEnabled(configDecides);
        if (!exits.isEmpty()) {
            form.form->addRow(QStringLiteral("Выходной IP"), exit);
            if (!configDecides) {
                auto *hint = new QLabel(QStringLiteral("Сейчас действует настройка сервера. Выбор появится, когда в настройках сервера приоритет выходного IP будет «Настройка конфига»."), &form.dialog);
                hint->setWordWrap(true);
                form.form->addRow(hint);
            }
        }
        auto *room = new QComboBox(&form.dialog);
        room->addItem(QStringLiteral("Комната сервера"), false); room->addItem(QStringLiteral("Без комнаты"), true);
        room->setCurrentIndex(client["link_room_off"].toBool() ? 1 : 0);
        form.form->addRow(QStringLiteral("Комната Jitsi"), room);
        const auto roomVisible = [=] {
            const bool jitsi = transport->currentData() == "jitsi";
            room->setVisible(jitsi);
            form.form->labelForField(room)->setVisible(jitsi);
        };
        QObject::connect(transport, &QComboBox::currentIndexChanged, &form.dialog, roomVisible);
        roomVisible();
        auto *hashSource = new QComboBox(&form.dialog);
        hashSource->addItem(QStringLiteral("Свои хеши"), "manual"); hashSource->addItem(QStringLiteral("Аккаунт сервера"), "server"); hashSource->addItem(QStringLiteral("Без хешей"), "client");
        hashSource->setCurrentIndex(client["vk_token"].toBool() ? 1 : client["vk_hashes"].toString().isEmpty() ? 2 : 0);
        form.form->addRow(QStringLiteral("Хеши VK"), hashSource);
        auto *hashes = new HashFields(&form.dialog, hashList(client["vk_hashes"].toString()), 6);
        form.form->addRow(hashes);
        auto *hashHint = new QLabel(&form.dialog);
        hashHint->setWordWrap(true);
        form.form->addRow(hashHint);
        const auto hashVisible = [=] {
            const auto source = hashSource->currentData().toString();
            hashes->setVisible(source == "manual");
            hashHint->setVisible(source != "manual");
            hashHint->setText(source == "server" ? QStringLiteral("В ссылку идут хеши с VK-аккаунта, привязанного к серверу.") : QStringLiteral("В ссылке нет хешей: клиент использует свой аккаунт VK или свои хеши."));
        };
        QObject::connect(hashSource, &QComboBox::currentIndexChanged, &form.dialog, hashVisible);
        hashVisible();
        if (!creating) {
            const auto password = client["password"].toString();
            auto *deviceBox = new QGroupBox(QStringLiteral("Устройства"), &form.dialog);
            auto *deviceLayout = new QVBoxLayout(deviceBox);
            const auto list = client["devices"].toArray();
            if (list.isEmpty()) deviceLayout->addWidget(new QLabel(QStringLiteral("Нет привязанных устройств"), deviceBox));
            for (const auto &entry : list) {
                const auto device = entry.toObject();
                auto *row = new QHBoxLayout;
                const auto seen = qint64(device["last_seen"].toDouble());
                auto *label = new QLabel((device["online"].toBool() ? QStringLiteral("● ") : QStringLiteral("○ ")) + textOr(device["name"], device["id"].toString().left(8)) +
                    QStringLiteral(" · %1 · %2").arg(device["ip"].toString(), seen > 0 ? timeText(seen) : QStringLiteral("—")), deviceBox);
                row->addWidget(label, 1);
                auto *unbind = new QPushButton(QStringLiteral("Отвязать"), deviceBox);
                QObject::connect(unbind, &QPushButton::clicked, &form.dialog, [this, password, id = device["id"].toString(), &form] {
                    form.dialog.reject();
                    changed("POST", "/api/clients/" + enc(password) + "/unbind?device=" + enc(id), {}, QStringLiteral("Устройство отвязано"));
                });
                row->addWidget(unbind);
                deviceLayout->addLayout(row);
            }
            if (!list.isEmpty()) {
                auto *all = new QPushButton(QStringLiteral("Отвязать все устройства"), deviceBox);
                QObject::connect(all, &QPushButton::clicked, &form.dialog, [this, password, &form] {
                    form.dialog.reject();
                    changed("POST", "/api/clients/" + enc(password) + "/unbind?device=", {}, QStringLiteral("Устройство отвязано"));
                });
                deviceLayout->addWidget(all);
            }
            form.form->addRow(deviceBox);
            form.form->addRow(new QLabel(QStringLiteral("Трафик: ↑ %1 ↓ %2 · сессий %3").arg(bytesText(client["up"].toDouble()), bytesText(client["down"].toDouble())).arg(client["active_sessions"].toInt()), &form.dialog));
            auto *chips = new QGridLayout;
            int chip = 0;
            const auto action = [&](const QString &text, const std::function<void()> &run) {
                auto *b = new QPushButton(text, &form.dialog);
                QObject::connect(b, &QPushButton::clicked, &form.dialog, run);
                chips->addWidget(b, chip / 3, chip % 3);
                chip++;
            };
            const auto link = client["link"].toString(), title = client["name"].toString();
            action(QStringLiteral("Ссылка и QR"), [this, title, link] { shareLink(title, link); });
            action(QStringLiteral("В мои профили"), [this, link] { if (MW_handle_deeplink) MW_handle_deeplink(link); status->setText(QStringLiteral("Добавлено в профили")); });
            if (!owner) {
                action(client["active"].toBool() ? QStringLiteral("Выключить") : QStringLiteral("Включить"), [this, password, &form] { form.dialog.reject(); changed("POST", "/api/clients/" + enc(password) + "/toggle", {}, QStringLiteral("Статус изменён")); });
                action(QStringLiteral("+30 дней"), [this, password, &form] { form.dialog.reject(); changed("POST", "/api/clients/" + enc(password) + "/extend", {{"days", 30}}, QStringLiteral("Продлено на 30 дней")); });
            }
            action(QStringLiteral("Сброс трафика"), [this, password, &form] { form.dialog.reject(); changed("POST", "/api/clients/" + enc(password) + "/reset-traffic", {}, QStringLiteral("Трафик сброшен")); });
            if (!owner) action(QStringLiteral("Удалить"), [this, password, title, &form] {
                if (!confirm(QStringLiteral("Удалить конфиг"), QStringLiteral("Удалить «%1»? Ссылка перестанет работать на всех устройствах.").arg(title))) return;
                form.dialog.reject();
                changed("DELETE", "/api/clients/" + enc(password), {}, QStringLiteral("Конфиг удалён"));
            });
            form.layout->addLayout(chips);
        }
        const auto valid = [=, &form] {
            if (!owner && name->text().trimmed().isEmpty()) { QMessageBox::warning(&form.dialog, form.dialog.windowTitle(), QStringLiteral("Укажите имя")); return false; }
            if (workers->value() != 0 && workers->value() < 3) { QMessageBox::warning(&form.dialog, form.dialog.windowTitle(), QStringLiteral("Потоки: 0 или от 3 до 100")); return false; }
            return true;
        };
        if (!form.exec(valid)) return;
        const bool serverHashes = hashSource->currentData() == "server";
        QJsonObject body{{"name", name->text().trimmed().left(128)}, {"group_id", group->currentData().toString()}, {"days", days->value()},
            {"hash", hashSource->currentData() == "manual" ? hashes->hashes().join(',') : QString()}, {"vk_token", serverHashes}, {"workers", workers->value()},
            {"max_devices", devices->value()}, {"transport", transport->currentData().toString()}, {"link_room_off", room->currentData().toBool()}, {"exit", exit->currentData().toString()}};
        if (creating) { changed("POST", "/api/clients", body, QStringLiteral("Конфиг создан")); return; }
        body["peer_port"] = client["peer_port"].toInt();
        body["local_port"] = client["local_port"].toInt();
        changed("POST", "/api/clients/" + enc(client["password"].toString()), body, QStringLiteral("Сохранено"));
    }
    void buildSubscription() {
        FormDialog form(this, QStringLiteral("Новая подписка"));
        auto *name = new QLineEdit(&form.dialog);
        name->setMaxLength(64);
        form.form->addRow(QStringLiteral("Название подписки"), name);
        auto *hint = new QLabel(QStringLiteral("Подписка: одна ссылка и QR-код, внутри которых выбранные конфиги. Тот, кто её добавит, получит их все в разделе «Профили»."), &form.dialog);
        hint->setWordWrap(true);
        form.form->addRow(hint);
        auto *list = new QListWidget(&form.dialog);
        for (const auto &entry : clients) {
            auto *item = new QListWidgetItem(entry.toObject()["name"].toString(), list);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        }
        form.form->addRow(list);
        if (!form.exec([=, &form] {
                bool any = false;
                for (int i = 0; i < list->count(); ++i) any = any || list->item(i)->checkState() == Qt::Checked;
                if (name->text().trimmed().isEmpty() || !any) { QMessageBox::warning(&form.dialog, form.dialog.windowTitle(), QStringLiteral("Укажите название и выберите конфиги")); return false; }
                return true;
            })) return;
        QJsonArray links;
        for (int i = 0; i < list->count(); ++i) if (list->item(i)->checkState() == Qt::Checked) {
            const auto client = clients[i].toObject();
            links.append(QJsonObject{{"name", client["name"]}, {"link", client["link"]}});
        }
        const auto payload = compact({{"v", 1}, {"name", name->text().trimmed()}, {"links", links}});
        const auto link = Configs::mrtt::SharePayload(payload, true);
        if (link.isEmpty()) { status->setText(QStringLiteral("Не удалось создать подписку")); return; }
        shareLink(name->text().trimmed(), link);
    }
    QWidget *buildClients() {
        auto *page = new QWidget(tabs);
        auto *layout = new QVBoxLayout(page);
        auto *top = new QHBoxLayout;
        button(QStringLiteral("Создать конфиг"), top, [this] { editClient({}); });
        button(QStringLiteral("Создать подписку"), top, [this] { buildSubscription(); });
        layout->addLayout(top);
        auto *filter = new QHBoxLayout;
        search = new QLineEdit(page);
        search->setPlaceholderText(QStringLiteral("Поиск по имени"));
        groupFilter = new QComboBox(page);
        filter->addWidget(search, 1);
        filter->addWidget(groupFilter);
        layout->addLayout(filter);
        connect(search, &QLineEdit::textChanged, this, [this] { showClients(); });
        connect(groupFilter, &QComboBox::currentIndexChanged, this, [this] { showClients(); });
        clientTable = table(page, {QStringLiteral("Имя"), QStringLiteral("Статус"), QStringLiteral("Группа"), QStringLiteral("Срок"), QStringLiteral("Устройства"), QStringLiteral("Трафик")});
        clientTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
        clientTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        layout->addWidget(clientTable, 1);
        connect(clientTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
            const auto password = clientTable->item(row, 0)->data(Qt::UserRole).toString();
            for (const auto &entry : clients) if (entry.toObject()["password"].toString() == password) { editClient(entry.toObject()); return; }
        });
        auto *chosen = new QHBoxLayout;
        chosen->addWidget(new QLabel(QStringLiteral("Выбранные:"), page));
        button(QStringLiteral("Вкл"), chosen, [this] { batch("toggle", {{"active", true}}, QStringLiteral("Включены")); });
        button(QStringLiteral("Выкл"), chosen, [this] { batch("toggle", {{"active", false}}, QStringLiteral("Выключены")); });
        button(QStringLiteral("+30 дней"), chosen, [this] { batch("extend", {{"days", 30}}, QStringLiteral("Продлены на 30 дней")); });
        button(QStringLiteral("Сброс трафика"), chosen, [this] { batch("reset-traffic", {}, QStringLiteral("Трафик сброшен")); });
        button(QStringLiteral("В группу"), chosen, [this] {
            QStringList names{QStringLiteral("Без группы")};
            for (const auto &entry : groups) names << entry.toObject()["name"].toString();
            bool ok = false;
            const auto picked = QInputDialog::getItem(this, QStringLiteral("Перенести в группу"), QStringLiteral("Группа"), names, 0, false, &ok);
            if (!ok) return;
            const int index = names.indexOf(picked);
            batch("group", {{"group_id", index == 0 ? QString() : groups[index - 1].toObject()["id"].toString()}}, QStringLiteral("Перенесено"));
        });
        button(QStringLiteral("Удалить"), chosen, [this] {
            const auto count = selectedPasswords().size();
            if (count && confirm(QStringLiteral("Удалить конфиги"), QStringLiteral("Удалить выбранные конфиги (%1)? Ссылки перестанут работать.").arg(count))) batch("delete", {}, QStringLiteral("Удалено"));
        });
        layout->addLayout(chosen);
        return page;
    }
    QWidget *buildGroups() {
        auto *page = new QWidget(tabs);
        auto *layout = new QVBoxLayout(page);
        button(QStringLiteral("Создать группу"), layout, [this] {
            const auto name = QInputDialog::getText(this, QStringLiteral("Новая группа"), QStringLiteral("Название группы")).trimmed().left(64);
            if (!name.isEmpty()) changed("POST", "/api/groups", {{"name", name}}, QStringLiteral("Группа создана"));
        });
        groupTable = table(page, {QStringLiteral("Группа"), QStringLiteral("Подписок"), QStringLiteral("В сети"), QStringLiteral("Трафик"), QStringLiteral("Маршрут")});
        groupTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        layout->addWidget(groupTable, 1);
        connect(groupTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
            const auto group = groups[row].toObject();
            FormDialog form(this, group["name"].toString());
            auto *name = new QLineEdit(group["name"].toString(), &form.dialog);
            form.form->addRow(QStringLiteral("Название группы"), name);
            auto *rule = new QComboBox(&form.dialog);
            rule->addItem(QStringLiteral("Нет"), QString());
            for (const auto &entry : rules) rule->addItem(entry.toObject()["name"].toString(), entry.toObject()["id"].toString());
            rule->setCurrentIndex(qMax(0, rule->findData(group["routing_rule_id"].toString())));
            form.form->addRow(QStringLiteral("Маршрутизация"), rule);
            auto *remove = new QPushButton(QStringLiteral("Удалить группу"), &form.dialog);
            form.form->addRow(remove);
            QObject::connect(remove, &QPushButton::clicked, &form.dialog, [&] {
                if (!confirm(QStringLiteral("Удалить группу"), QStringLiteral("Удалить «%1» вместе со всеми её конфигами (%2)? Их ссылки перестанут работать.").arg(group["name"].toString()).arg(group["member_count"].toInt()))) return;
                form.dialog.reject();
                changed("DELETE", "/api/groups/" + enc(group["id"].toString()), {}, QStringLiteral("Группа удалена"));
            });
            if (form.exec()) changed("PUT", "/api/groups/" + enc(group["id"].toString()), {{"name", name->text().trimmed().left(64)}, {"routing_rule_id", rule->currentData().toString()}}, QStringLiteral("Сохранено"));
        });
        return page;
    }
    static QStringList lines(const QString &text) {
        QStringList out;
        for (const auto &item : text.split(QRegularExpression("[\\n,;]+"), Qt::SkipEmptyParts)) if (const auto value = item.trimmed(); !value.isEmpty() && !out.contains(value)) out << value;
        return out;
    }
    static QStringList strings(const QJsonValue &value) {
        QStringList out;
        for (const auto &item : value.toArray()) out << item.toString();
        return out;
    }
    void editRule(const QJsonObject &rule) {
        const bool creating = rule.isEmpty();
        FormDialog form(this, creating ? QStringLiteral("Новое правило") : rule["name"].toString());
        const auto mode = [&](const QString &label, bool white) {
            auto *box = new QComboBox(&form.dialog);
            box->addItem(QStringLiteral("Чёрный список"), false); box->addItem(QStringLiteral("Белый список"), true);
            box->setCurrentIndex(white ? 1 : 0);
            form.form->addRow(label, box);
            return box;
        };
        const auto area = [&](const QString &label, const QStringList &items) {
            auto *edit = new QPlainTextEdit(items.join('\n'), &form.dialog);
            edit->setMaximumHeight(80);
            form.form->addRow(label, edit);
            return edit;
        };
        auto *name = new QLineEdit(rule["name"].toString(), &form.dialog);
        form.form->addRow(QStringLiteral("Название"), name);
        auto *apps = mode(QStringLiteral("Режим исключения приложений"), rule["is_whitelist"].toBool());
        auto *packages = area(QStringLiteral("Пакеты, по одному в строке"), strings(rule["packages"]));
        auto *domainsMode = mode(QStringLiteral("Режим исключения доменов"), rule["domains_is_whitelist"].toBool());
        auto *templateDomains = new QCheckBox(QStringLiteral("Готовый список RU доменов"), &form.dialog);
        templateDomains->setChecked(rule["template_domains"].toBool());
        form.form->addRow(templateDomains);
        auto *domains = area(QStringLiteral("Домены, по одному в строке"), strings(rule["direct_domains"]));
        auto *ipMode = mode(QStringLiteral("Режим исключения адресов"), rule["ip_is_whitelist"].toBool());
        auto *ips = area(QStringLiteral("Адреса: IP, CIDR или диапазон, по одному в строке"), strings(rule["ips"]));
        const auto checks = [&](const QString &label, const QJsonArray &items, const QString &key, const QStringList &assigned) {
            auto *list = new QListWidget(&form.dialog);
            list->setMaximumHeight(110);
            for (const auto &entry : items) {
                const auto object = entry.toObject();
                auto *item = new QListWidgetItem(object["name"].toString(), list);
                item->setData(Qt::UserRole, object[key].toString());
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(assigned.contains(object[key].toString()) ? Qt::Checked : Qt::Unchecked);
            }
            form.form->addRow(label, list);
            return list;
        };
        auto *assignedGroups = checks(QStringLiteral("Применить к группам"), groups, "id", strings(rule["assigned_groups"]));
        auto *assignedClients = checks(QStringLiteral("Применить к конфигам"), clients, "password", strings(rule["assigned_clients"]));
        if (!creating) {
            auto *remove = new QPushButton(QStringLiteral("Удалить правило"), &form.dialog);
            form.form->addRow(remove);
            QObject::connect(remove, &QPushButton::clicked, &form.dialog, [&] { form.dialog.reject(); changed("DELETE", "/api/routing-rules/" + enc(rule["id"].toString()), {}, QStringLiteral("Правило удалено")); });
        }
        if (!form.exec()) return;
        const auto picked = [](QListWidget *list) {
            QStringList out;
            for (int i = 0; i < list->count(); ++i) if (list->item(i)->checkState() == Qt::Checked) out << list->item(i)->data(Qt::UserRole).toString();
            return QJsonArray::fromStringList(out);
        };
        QJsonObject body{{"name", name->text().trimmed().isEmpty() ? QStringLiteral("Правило") : name->text().trimmed().left(64)}, {"is_whitelist", apps->currentData().toBool()},
            {"packages", QJsonArray::fromStringList(lines(packages->toPlainText()))}, {"template_domains", templateDomains->isChecked()}, {"domains_is_whitelist", domainsMode->currentData().toBool()},
            {"direct_domains", QJsonArray::fromStringList(lines(domains->toPlainText()))}, {"ip_is_whitelist", ipMode->currentData().toBool()}, {"ips", QJsonArray::fromStringList(lines(ips->toPlainText()))},
            {"assigned_groups", picked(assignedGroups)}, {"assigned_clients", picked(assignedClients)}};
        if (creating) changed("POST", "/api/routing-rules", body, QStringLiteral("Правило сохранено"));
        else changed("PUT", "/api/routing-rules/" + enc(rule["id"].toString()), body, QStringLiteral("Правило сохранено"));
    }
    QWidget *buildRules() {
        auto *page = new QWidget(tabs);
        auto *layout = new QVBoxLayout(page);
        auto *hint = new QLabel(QStringLiteral("Правила раздельной маршрутизации, которые сервер отправляет клиентам: какие приложения, домены и IP идут мимо туннеля."), page);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        button(QStringLiteral("Создать правило"), layout, [this] { editRule({}); });
        ruleTable = table(page, {QStringLiteral("Правило"), QStringLiteral("Состав")});
        ruleTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        layout->addWidget(ruleTable, 1);
        connect(ruleTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { editRule(rules[row].toObject()); });
        return page;
    }
    void editProxy(const QJsonObject &profile) {
        const bool creating = profile.isEmpty();
        FormDialog form(this, creating ? QStringLiteral("Новый SOCKS5") : profile["name"].toString());
        auto *name = new QLineEdit(profile["name"].toString(), &form.dialog);
        auto *port = spin(&form.dialog, 1, 65535, profile["port"].toInt(45000));
        auto *user = new QLineEdit(profile["username"].toString(), &form.dialog);
        auto *secret = new QLineEdit(profile["password"].toString(), &form.dialog);
        secret->setEchoMode(QLineEdit::Password);
        form.form->addRow(QStringLiteral("Название"), name);
        form.form->addRow(QStringLiteral("Порт на 127.0.0.1"), port);
        form.form->addRow(QStringLiteral("Логин (необязательно)"), user);
        form.form->addRow(QStringLiteral("Пароль (необязательно)"), secret);
        if (!creating) {
            auto *remove = new QPushButton(QStringLiteral("Удалить профиль"), &form.dialog);
            form.form->addRow(remove);
            QObject::connect(remove, &QPushButton::clicked, &form.dialog, [&] { form.dialog.reject(); changed("DELETE", "/api/local-proxy/profiles/" + enc(profile["id"].toString()), {}, QStringLiteral("Удалено")); });
        }
        if (!form.exec()) return;
        QJsonObject body{{"name", name->text().trimmed().left(64)}, {"port", port->value()}, {"username", user->text()}, {"password", secret->text()}};
        if (creating) changed("POST", "/api/local-proxy", body, QStringLiteral("Сохранено"));
        else changed("PUT", "/api/local-proxy/profiles/" + enc(profile["id"].toString()), body, QStringLiteral("Сохранено"));
    }
    QWidget *buildProxy() {
        auto *page = new QWidget(tabs);
        auto *layout = new QVBoxLayout(page);
        auto *hint = new QLabel(QStringLiteral("Локальный SOCKS5 на сервере: весь трафик клиентов можно отправить через прокси, запущенный на этом же VPS (например, каскад)."), page);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        proxyState = new QLabel(page);
        proxyState->setWordWrap(true);
        layout->addWidget(proxyState);
        auto *row = new QHBoxLayout;
        button(QStringLiteral("Добавить профиль SOCKS5"), row, [this] { editProxy({}); });
        button(QStringLiteral("Сделать активным"), row, [this] {
            if (const int current = proxyTable->currentRow(); current >= 0) changed("POST", "/api/local-proxy/activate/" + enc(proxies[current].toObject()["id"].toString()), {}, QStringLiteral("Прокси включён"));
        });
        button(QStringLiteral("Выключить"), row, [this] { changed("POST", "/api/local-proxy/deactivate", {}, QStringLiteral("Прокси выключен")); });
        layout->addLayout(row);
        proxyTable = table(page, {QStringLiteral("Профиль"), QStringLiteral("Адрес")});
        proxyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        layout->addWidget(proxyTable, 1);
        connect(proxyTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { editProxy(proxies[row].toObject()); });
        return page;
    }
    void openSettings() {
        api("GET", "/api/settings", {}, [this](const QString &text) {
            settings = QJsonDocument::fromJson(text.toUtf8()).object();
            FormDialog form(this, QStringLiteral("Настройки сервера"));
            if (settings["restart_required"].toBool()) form.form->addRow(new QLabel(QStringLiteral("Пароль владельца изменён: перезапустите сервер, чтобы он применился."), &form.dialog));
            auto *owner = new QLineEdit(settings["main_password"].toString(), &form.dialog);
            auto *ownerRow = new QHBoxLayout;
            ownerRow->addWidget(owner, 1);
            auto *generate = new QPushButton(QStringLiteral("Сгенерировать"), &form.dialog);
            ownerRow->addWidget(generate);
            QObject::connect(generate, &QPushButton::clicked, &form.dialog, [owner] {
                static const QString alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
                QString value;
                for (int i = 0; i < 16; ++i) value += alphabet[QRandomGenerator::system()->bounded(int(alphabet.size()))];
                owner->setText(value);
            });
            form.form->addRow(QStringLiteral("Пароль владельца"), ownerRow);
            auto *dns = new QComboBox(&form.dialog);
            const QList<QPair<QString, QString>> providers{{"yandex", "Yandex DNS"}, {"cloudflare", "Cloudflare"}, {"google", "Google"}, {"adguard", "AdGuard"}, {"xbox", "Xbox DNS"}, {"comms", "Comss DNS"}, {"geohide", "GeoHide DNS"}, {"quad9", "Quad9"}, {"opendns", "OpenDNS"}, {"rostelecom", "Rostelecom DNS"}, {"nextdns", "NextDNS"}, {"bizone", "BI.ZONE DNS"}, {"nsdi", QStringLiteral("НСДИ DNS")}, {"custom", QStringLiteral("Ручной DNS")}};
            for (const auto &[key, title] : providers) dns->addItem(title, key);
            dns->setCurrentIndex(qMax(0, dns->findData(settings["dns_provider"].toString("yandex"))));
            form.form->addRow(QStringLiteral("DNS для клиентов"), dns);
            auto *primary = new QLineEdit(settings["dns_primary"].toString(), &form.dialog);
            auto *secondary = new QLineEdit(settings["dns_secondary"].toString(), &form.dialog);
            form.form->addRow(QStringLiteral("Основной IPv4"), primary);
            form.form->addRow(QStringLiteral("Запасной IPv4 (необязательно)"), secondary);
            const auto dnsVisible = [=, &form] {
                const bool custom = dns->currentData() == "custom";
                for (QWidget *field : {static_cast<QWidget *>(primary), static_cast<QWidget *>(secondary)}) { field->setVisible(custom); form.form->labelForField(field)->setVisible(custom); }
            };
            QObject::connect(dns, &QComboBox::currentIndexChanged, &form.dialog, dnsVisible);
            dnsVisible();
            auto *restart = new QComboBox(&form.dialog);
            for (const auto &[hours, title] : QList<QPair<int, QString>>{{0, QStringLiteral("Выключен")}, {12, QStringLiteral("12 ч")}, {24, QStringLiteral("24 ч")}, {48, QStringLiteral("48 ч")}, {72, QStringLiteral("72 ч")}}) restart->addItem(title, hours);
            restart->setCurrentIndex(qMax(0, restart->findData(settings["auto_restart_interval_hours"].toInt())));
            form.form->addRow(QStringLiteral("Автоперезапуск"), restart);
            auto vkToken = std::make_shared<QString>(settings["vk_token"].toString());
            auto *vkState = new QLabel(&form.dialog);
            vkState->setWordWrap(true);
            auto *vkBind = new QPushButton(&form.dialog);
            auto *vkUnbind = new QPushButton(QStringLiteral("Отвязать"), &form.dialog);
            const auto vkShow = [=] {
                vkState->setText(vkToken->isEmpty() ? QStringLiteral("Не привязан. Привяжите аккаунт, и конфиги с режимом «Аккаунт сервера» будут получать хеши автоматически.")
                                                    : QStringLiteral("Привязан. Конфиги с режимом «Аккаунт сервера» получают хеши с этого аккаунта автоматически."));
                vkBind->setText(vkToken->isEmpty() ? QStringLiteral("Привязать VK") : QStringLiteral("Перепривязать"));
                vkUnbind->setVisible(!vkToken->isEmpty());
            };
            vkShow();
            QObject::connect(vkBind, &QPushButton::clicked, &form.dialog, [=, &form] {
                // The client's one VK binding hands its token to the server.
                QString error;
                const auto token = RelayAccountToken(&error);
                if (!error.isEmpty()) { QMessageBox::warning(&form.dialog, form.dialog.windowTitle(), error); return; }
                *vkToken = token;
                vkShow();
            });
            QObject::connect(vkUnbind, &QPushButton::clicked, &form.dialog, [=] { vkToken->clear(); vkShow(); });
            form.form->addRow(QStringLiteral("VK-аккаунт сервера"), vkState);
            auto *vkRow = new QHBoxLayout;
            vkRow->addWidget(vkBind);
            vkRow->addWidget(vkUnbind);
            form.form->addRow(vkRow);
            auto *vkHashes = new HashFields(&form.dialog, hashList(settings["vk_hashes"].toString()), 6);
            form.form->addRow(QStringLiteral("Общие VK-хеши, необязательно"), vkHashes);
            auto *applyAll = new QPushButton(QStringLiteral("Применить ко всем конфигам"), &form.dialog);
            form.form->addRow(applyAll);
            QObject::connect(applyAll, &QPushButton::clicked, &form.dialog, [=, this] {
                changed("POST", "/api/links/batch-vk", {{"vk_token", *vkToken}, {"vk_hashes", vkHashes->hashes().join(',')}, {"apply_to_all_clients", true}}, QStringLiteral("VK применён ко всем конфигам"));
            });
            auto *room = new QLineEdit(settings["jitsi_room"].toString().isEmpty() ? QString() : "https://" + settings["jitsi_room"].toString(), &form.dialog);
            room->setPlaceholderText("https://meet.example.org/room");
            form.form->addRow(QStringLiteral("Ссылка на комнату Jitsi"), room);
            auto *seats = new QComboBox(&form.dialog);
            for (int count : {2, 4, 6, 8, 12, 16}) seats->addItem(QString::number(count), count);
            seats->setCurrentIndex(qMax(0, seats->findData(settings["jitsi_seats"].toInt(6))));
            form.form->addRow(QStringLiteral("Участников в комнате"), seats);
            auto *roomHint = new QLabel(QStringLiteral("Создайте конференцию на любом сервере Jitsi и вставьте ссылку: сервер зайдёт в неё сразу после сохранения, переустанавливать не нужно."), &form.dialog);
            roomHint->setWordWrap(true);
            form.form->addRow(roomHint);
            auto *priority = new QComboBox(&form.dialog);
            priority->addItem(QStringLiteral("Настройка сервера"), QString()); priority->addItem(QStringLiteral("Настройка конфига"), "config");
            priority->setCurrentIndex(settings["exit_priority"].toString() == "config" ? 1 : 0);
            form.form->addRow(QStringLiteral("Приоритет выходного IP"), priority);
            auto *defaultExit = new QComboBox(&form.dialog);
            defaultExit->addItem(QStringLiteral("Этот сервер"), QString());
            const auto exits = settings["exits"].toArray();
            for (const auto &entry : exits) defaultExit->addItem(entry.toObject()["name"].toString(), entry.toObject()["id"].toString());
            defaultExit->setCurrentIndex(qMax(0, defaultExit->findData(settings["default_exit"].toString())));
            form.form->addRow(QStringLiteral("Выход сервера"), defaultExit);
            auto *exitHint = new QLabel(QStringLiteral("Трафик клиентов выходит через выбранный VPS по протоколу MRTT, UDP тоже. Если связь между серверами упадёт, такие клиенты теряют интернет, а не выходят через этот сервер."), &form.dialog);
            exitHint->setWordWrap(true);
            form.form->addRow(exitHint);
            if (!form.exec([=, &form] {
                    if (!Configs::mrtt::ValidPassword(owner->text())) { QMessageBox::warning(&form.dialog, form.dialog.windowTitle(), QStringLiteral("Пароль владельца: без пробелов и «|», не длиннее 128 байт")); return false; }
                    if (!room->text().trimmed().isEmpty() && Configs::mrtt::ParseJitsiRoom(room->text()).isEmpty()) { QMessageBox::warning(&form.dialog, form.dialog.windowTitle(), QStringLiteral("Неверная ссылка на комнату Jitsi")); return false; }
                    return true;
                })) return;
            QJsonObject body{{"default_exit", defaultExit->currentData().toString()}, {"exit_priority", priority->currentData().toString()}, {"main_password", owner->text()},
                {"dns_provider", dns->currentData().toString()}, {"auto_restart_interval_hours", restart->currentData().toInt()}, {"vk_token", *vkToken},
                {"vk_hashes", vkHashes->hashes().join(',')}, {"jitsi_room", Configs::mrtt::ParseJitsiRoom(room->text())}, {"jitsi_seats", seats->currentData().toInt()}};
            if (dns->currentData() == "custom") { body["dns_primary"] = primary->text().trimmed(); body["dns_secondary"] = secondary->text().trimmed(); }
            changed("POST", "/api/settings", body, QStringLiteral("Настройки сохранены"));
        });
    }
    // Exits of the cascade: other MRTT servers of this client, with the access stored for them.
    void addExit() {
        task->start({{"operation", "secret-get"}, {"id", "servers"}}, [this](const QJsonObject &event) {
            if (event["type"] != "result") return;
            QList<QJsonObject> candidates;
            const auto exits = settings["exits"].toArray();
            for (const auto &entry : QJsonDocument::fromJson(event["value"].toString().toUtf8()).array()) {
                const auto other = entry.toObject();
                if (other["kind"] != "mrtt" || other["id"] == server["id"] || !installed(other)) continue;
                bool used = false;
                for (const auto &exit : exits) used = used || exit.toObject()["host"] == other["host"];
                if (!used) candidates.append(other);
            }
            if (candidates.isEmpty()) { status->setText(QStringLiteral("Других серверов нет: добавьте VPS в разделе деплоя MRTT.")); return; }
            QStringList names;
            for (const auto &other : candidates) names << textOr(other["name"], other["host"].toString());
            bool ok = false;
            const auto picked = QInputDialog::getItem(this, QStringLiteral("Выход через другой VPS"),
                QStringLiteral("Этот сервер подключится к выбранному как обычный конфиг, он появится там в списке конфигов. Удалять его там не нужно, для этого есть кнопка «Убрать» здесь."), names, 0, false, &ok);
            if (!ok) return;
            const auto target = candidates[names.indexOf(picked)];
            call("mrtt-admin", {{"action", "exit-add"}, {"target", target}}, [this](const QJsonValue &created) {
                auto list = settings["exits"].toArray();
                list.append(created.toObject());
                changed("POST", "/api/settings", {{"exits", list}, {"default_exit", settings["default_exit"].toString()}}, QStringLiteral("Выход добавлен"));
            });
        });
    }
    void removeExit() {
        const auto exits = settings["exits"].toArray();
        if (exits.isEmpty()) { status->setText(QStringLiteral("Выходов нет")); return; }
        QStringList names;
        for (const auto &exit : exits) names << exit.toObject()["name"].toString() + " · " + exit.toObject()["host"].toString();
        bool ok = false;
        const auto picked = QInputDialog::getItem(this, QStringLiteral("Убрать выход"), QStringLiteral("Конфиги с этим выходом пойдут через этот сервер."), names, 0, false, &ok);
        if (!ok) return;
        const int index = names.indexOf(picked);
        const auto exit = exits[index].toObject();
        auto remaining = exits;
        remaining.removeAt(index);
        // Settings first: clients must not stay routed into a table that is gone.
        api("POST", "/api/settings", {{"exits", remaining}, {"default_exit", settings["default_exit"].toString() == exit["id"].toString() ? QString() : settings["default_exit"].toString()}}, [this, exit](const QString &) {
            task->start({{"operation", "secret-get"}, {"id", "servers"}}, [this, exit](const QJsonObject &event) {
                if (event["type"] != "result") return;
                QJsonObject target;
                for (const auto &entry : QJsonDocument::fromJson(event["value"].toString().toUtf8()).array())
                    if (entry.toObject()["kind"] == "mrtt" && entry.toObject()["host"] == exit["host"]) target = entry.toObject();
                QJsonObject request{{"action", "exit-remove"}, {"id", exit["id"]}, {"remote_password", exit["remote_password"]}};
                if (!target.isEmpty()) request["target"] = target;
                call("mrtt-admin", request, [this](const QJsonValue &) { reload(); status->setText(QStringLiteral("Выход убран")); });
            });
        });
    }
    QWidget *buildSettings() {
        auto *page = new QWidget(tabs);
        auto *layout = new QVBoxLayout(page);
        button(QStringLiteral("Настройки сервера"), layout, [this] { openSettings(); });
        auto *exits = new QHBoxLayout;
        button(QStringLiteral("Добавить выход через другой VPS"), exits, [this] { addExit(); });
        button(QStringLiteral("Убрать выход"), exits, [this] { removeExit(); });
        layout->addLayout(exits);
        auto *service = new QGroupBox(QStringLiteral("Обслуживание"), page);
        auto *row = new QHBoxLayout(service);
        button(QStringLiteral("Экспорт"), row, [this] {
            const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Экспорт конфигурации"), "mrtt-config.json", "JSON (*.json)");
            if (!path.isEmpty()) api("GET", "/api/export", {}, [this, path](const QString &text) {
                QFile file(path);
                if (file.open(QIODevice::WriteOnly)) { file.write(text.toUtf8()); status->setText(QStringLiteral("Конфигурация сохранена")); }
                else status->setText(file.errorString());
            });
        });
        button(QStringLiteral("Импорт"), row, [this] {
            const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Импорт конфигурации"), {}, "JSON (*.json);;*");
            if (path.isEmpty()) return;
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) { status->setText(file.errorString()); return; }
            const auto text = QString::fromUtf8(file.readAll());
            if (!confirm(QStringLiteral("Импорт конфигурации"), QStringLiteral("Добавить конфиги, группы и правила из файла и применить его настройки? Существующие подписки сохранятся."))) return;
            call("mrtt-admin", {{"method", "POST"}, {"path", "/api/import"}, {"body", text}}, [this](const QJsonValue &) { reload(); status->setText(QStringLiteral("Импорт выполнен")); });
        });
        button(QStringLiteral("Очистить память"), row, [this] { changed("POST", "/api/memory/trim", {}, QStringLiteral("Память освобождена")); });
        button(QStringLiteral("Перезапустить"), row, [this] {
            if (confirm(QStringLiteral("Перезапуск сервера"), QStringLiteral("Перезапустить MRTT на сервере? Все клиенты переподключатся."))) changed("POST", "/api/reboot", {}, QStringLiteral("Перезапуск запрошен"));
        });
        layout->addWidget(service);
        layout->addStretch();
        return page;
    }
    QWidget *buildStats() {
        auto *page = new QWidget(tabs);
        statsForm = new QFormLayout(page);
        return page;
    }
    void loadStats() {
        api("GET", "/api/stats", {}, [this](const QString &text) {
            const auto s = QJsonDocument::fromJson(text.toUtf8()).object();
            const qint64 uptime = qint64(s["uptime"].toDouble());
            const QList<QPair<QString, QString>> rows{
                {QStringLiteral("Статус"), s["status"].toString()}, {QStringLiteral("Публичный IP"), s["public_ip"].toString()}, {QStringLiteral("Порт подключения"), QString::number(s["peer_port"].toInt())},
                {QStringLiteral("Аптайм"), QStringLiteral("%1 д %2 ч %3 мин").arg(uptime / 86400).arg(uptime % 86400 / 3600).arg(uptime % 3600 / 60)},
                {QStringLiteral("Сессий сейчас"), QString::number(s["active"].toInt())}, {QStringLiteral("Подключений всего"), QString::number(qint64(s["total"].toDouble()))}, {QStringLiteral("Подписок"), QString::number(s["passwords"].toInt())},
                {"CPU", QString::number(s["cpu_normalized"].toInt()) + "%"}, {QStringLiteral("Память"), s["ram"].toString()}, {QStringLiteral("Пик памяти"), s["ram_peak"].toString()},
                {QStringLiteral("Трафик от клиентов"), bytesText(s["up"].toDouble())}, {QStringLiteral("Трафик к клиентам"), bytesText(s["down"].toDouble())},
                {QStringLiteral("UDP пакетов RX / TX"), QStringLiteral("%1 / %2").arg(qint64(s["udp_rx_packets"].toDouble())).arg(qint64(s["udp_tx_packets"].toDouble()))},
                {QStringLiteral("UDP ошибок RX / TX"), QStringLiteral("%1 / %2").arg(qint64(s["udp_rx_errors"].toDouble())).arg(qint64(s["udp_tx_errors"].toDouble()))}, {QStringLiteral("Аллокатор"), s["allocator"].toString()},
                {QStringLiteral("Локальный прокси"), s["local_proxy_active"].toBool() ? QStringLiteral("включён") : QStringLiteral("выключен")},
                {QStringLiteral("TCP / UDP через прокси"), QStringLiteral("%1 / %2").arg(s["local_proxy_tcp_sessions"].toInt()).arg(s["local_proxy_udp_flows"].toInt())},
                {QStringLiteral("Горячих сессий"), QStringLiteral("%1 / %2").arg(s["hot_sessions"].toInt()).arg(s["hot_session_capacity"].toInt())},
                {QStringLiteral("Буфер логов"), QStringLiteral("%1 строк").arg(s["mem_log_ring"].toInt())}, {QStringLiteral("DPI-кадров в буфере"), QString::number(s["mem_dpi_ring"].toInt())}};
            while (statsForm->rowCount() > 0) statsForm->removeRow(0);
            for (const auto &[label, value] : rows) statsForm->addRow(label, new QLabel(value.isEmpty() ? QStringLiteral("—") : value));
        });
    }
    QWidget *buildLogs() {
        auto *page = new QWidget(tabs);
        auto *layout = new QVBoxLayout(page);
        logging = new QCheckBox(QStringLiteral("Запись логов на сервере"), page);
        layout->addWidget(logging);
        connect(logging, &QCheckBox::clicked, this, [this](bool active) { api("POST", "/api/logs/toggle", {{"active", active}}, [this](const QString &) { loadLogs(); }); });
        logView = new QPlainTextEdit(page);
        logView->setReadOnly(true);
        logView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        layout->addWidget(logView, 1);
        auto *row = new QHBoxLayout;
        button(QStringLiteral("Копировать"), row, [this] { QApplication::clipboard()->setText(logView->toPlainText()); });
        button(QStringLiteral("Очистить логи"), row, [this] { api("POST", "/api/logs/clear", {}, [this](const QString &) { loadLogs(); status->setText(QStringLiteral("Логи очищены")); }); });
        layout->addLayout(row);
        return page;
    }
    void loadLogs() {
        api("GET", "/api/logs", {}, [this](const QString &text) {
            const auto json = QJsonDocument::fromJson(text.toUtf8()).object();
            const QSignalBlocker blocker(logging);
            logging->setChecked(json["active"].toBool());
            QStringList lines;
            for (const auto &line : json["lines"].toArray()) lines << line.toString();
            logView->setPlainText(lines.isEmpty() ? QStringLiteral("Логи пусты") : lines.join('\n'));
            logView->moveCursor(QTextCursor::End);
        });
    }
public:
    MrttServer(QWidget *parent, const QJsonObject &value) : ServerScreen(parent, value, QStringLiteral("Сервер MRTT")) {
        resize(860, 620);
        auto *body = new QVBoxLayout(this);
        tabs = new QTabWidget(this);
        body->addWidget(busy);
        body->addWidget(status);
        body->addWidget(tabs, 1);
        tabs->addTab(buildClients(), QStringLiteral("Конфиги"));
        tabs->addTab(buildGroups(), QStringLiteral("Группы"));
        tabs->addTab(buildRules(), QStringLiteral("Маршруты"));
        tabs->addTab(buildProxy(), QStringLiteral("Прокси"));
        tabs->addTab(buildSettings(), QStringLiteral("Настройки"));
        tabs->addTab(buildStats(), QStringLiteral("Статистика"));
        tabs->addTab(buildLogs(), QStringLiteral("Логи"));
        // Statistics refresh every 10 s, logs every 5 s while their section is open, as in the original.
        liveTimer = new QTimer(this);
        int *tick = new int(0);
        connect(this, &QObject::destroyed, this, [tick] { delete tick; });
        connect(liveTimer, &QTimer::timeout, this, [this, tick] {
            if (running > 0) return;
            ++*tick;
            if (tabs->currentIndex() == 5 && *tick % 2 == 0) loadStats();
            if (tabs->currentIndex() == 6) loadLogs();
        });
        liveTimer->start(5000);
        connect(tabs, &QTabWidget::currentChanged, this, [this](int index) { if (index == 5) loadStats(); if (index == 6) loadLogs(); });
        reload();
    }
};

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
    } else if (kind == "mrtt") {
        entry(QStringLiteral("Конфиги и сервер"), QStringLiteral("Конфиги, подписки, группы, маршруты, прокси, настройки, статистика и логи"), [=] {
            const auto s = current();
            needsInstall(installed(s) && !s["hostKey"].toString().isEmpty(), [=] { (new MrttServer(parent, s))->show(); });
        });
    } else if (kind == "freeturn") {
        entry(QStringLiteral("Клиенты"), QStringLiteral("Ссылки и WireGuard для каждого устройства"), [=] {
            const auto s = current();
            needsInstall(installed(s) && !s["hostKey"].toString().isEmpty(), [=] { (new FreeturnClients(parent, s))->show(); });
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
