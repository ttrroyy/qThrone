#include "include/ui/group/dialog_provider_routing.h"
#include "include/configs/sub/ProviderRouting.h"
#include "include/global/GuiUtils.hpp"
#include "include/global/Utils.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace PR = Subscription::ProviderRouting;
namespace {
bool editDocument(QJsonObject &doc, const QString &provider, QWidget *parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Профиль маршрутизации %1").arg(provider));
    dialog.resize(760, 650);
    auto layout = new QVBoxLayout(&dialog);
    auto tabs = new QTabWidget(&dialog);
    layout->addWidget(tabs);
    auto general = new QWidget;
    auto form = new QFormLayout(general);
    QMap<QString, QLineEdit *> fields;
    const auto field = [&](const QString &key, const QString &label, QFormLayout *target) {
        auto input = new QLineEdit(doc.value(key).toString());
        fields[key] = input;
        target->addRow(label, input);
        return input;
    };
    field("Name", QObject::tr("Название"), form);
    auto global = new QCheckBox(QObject::tr("Прокси по умолчанию"));
    global->setChecked(PR::Truth(doc, "GlobalProxy", true));
    form->addRow(global);
    auto fake = new QCheckBox("FakeDNS");
    fake->setChecked(PR::Truth(doc, "FakeDNS", false));
    form->addRow(fake);
    auto order = new QComboBox;
    order->addItems({"block-proxy-direct", "block-direct-proxy", "proxy-block-direct", "proxy-direct-block",
                     "direct-block-proxy", "direct-proxy-block"});
    order->setCurrentText(doc.value("RouteOrder").toString("block-proxy-direct"));
    form->addRow(QObject::tr("Порядок правил"), order);
    auto strategy = new QComboBox;
    strategy->addItems({"AsIs", "IPIfNonMatch", "IPOnDemand"});
    strategy->setCurrentText(doc.value("DomainStrategy").toString("AsIs"));
    form->addRow(QObject::tr("Разрешение доменов"), strategy);
    for (const auto &prefix : {QStringLiteral("Remote"), QStringLiteral("Domestic")}) {
        const auto label = prefix == "Remote" ? QObject::tr("DNS через прокси") : QObject::tr("Прямой DNS");
        auto type = new QComboBox;
        type->addItems({"UDP", "TCP", "DoH", "DoT"});
        type->setCurrentText(doc.value(prefix + "DNSType").toString("UDP"));
        type->setObjectName(prefix + "DNSType");
        form->addRow(label, type);
        field(prefix + "DNSIP", QObject::tr("IP DNS"), form);
        auto domain = field(prefix + "DNSDomain", QObject::tr("Адрес DNS"), form);
        QObject::connect(type, &QComboBox::currentTextChanged, domain,
                         [domain](const QString &value) { domain->setEnabled(value == "DoH" || value == "DoT"); });
        domain->setEnabled(type->currentText() == "DoH" || type->currentText() == "DoT");
    }
    tabs->addTab(general, QObject::tr("Основное"));
    QMap<QString, QPlainTextEdit *> lists;
    for (const auto &action : {QStringLiteral("Direct"), QStringLiteral("Proxy"), QStringLiteral("Block")}) {
        auto page = new QWidget;
        auto pageLayout = new QVBoxLayout(page);
        for (const auto &suffix : {QStringLiteral("Sites"), QStringLiteral("Ip")}) {
            auto box = new QGroupBox(suffix == "Sites" ? QObject::tr("Домены и GeoSite") : QObject::tr("IP и GeoIP"));
            auto boxLayout = new QVBoxLayout(box);
            auto edit = new QPlainTextEdit;
            QStringList entries;
            for (const auto &value : doc.value(action + suffix).toArray())
                entries << value.toString();
            edit->setPlainText(entries.join('\n'));
            edit->setPlaceholderText(suffix == "Sites" ? "domain:example.com\nfull:host.example.com\ngeosite:WHITELIST"
                                                       : "192.0.2.0/24\ngeoip:ru");
            lists[action + suffix] = edit;
            boxLayout->addWidget(edit);
            pageLayout->addWidget(box);
        }
        tabs->addTab(page, action == "Direct"  ? QObject::tr("Напрямую")
                           : action == "Proxy" ? QObject::tr("Прокси")
                                               : QObject::tr("Блокировка"));
    }
    auto geo = new QWidget;
    auto geoLayout = new QFormLayout(geo);
    auto site = field("Geositeurl", "GeoSite URL", geoLayout);
    site->setPlaceholderText(PR::GeoUrl({}, "geosite"));
    auto ip = field("Geoipurl", "GeoIP URL", geoLayout);
    ip->setPlaceholderText(PR::GeoUrl({}, "geoip"));
    auto hosts = new QPlainTextEdit(QJsonDocument(doc.value("DnsHosts").toObject()).toJson(QJsonDocument::Indented));
    geoLayout->addRow(QObject::tr("DNS hosts (JSON)"), hosts);
    auto status = new QLabel;
    status->setWordWrap(true);
    geoLayout->addRow(status);
    for (const auto &kind : {QStringLiteral("geosite"), QStringLiteral("geoip")}) {
        auto download = new QPushButton(QObject::tr("Обновить %1").arg(kind));
        geoLayout->addRow(download);
        QObject::connect(download, &QPushButton::clicked, &dialog, [&, kind, download] {
            auto document = doc;
            document["Geositeurl"] = site->text();
            document["Geoipurl"] = ip->text();
            QPointer<QLabel> report(status);
            QPointer<QPushButton> button(download);
            download->setEnabled(false);
            status->setText(QObject::tr("Загрузка %1…").arg(kind));
            runOnNewThread([document, kind, report, button] {
                QString path;
                const auto error = PR::PrepareGeo(document, kind, path, true);
                runOnUiThread([error, kind, report, button] {
                    if (button)
                        button->setEnabled(true);
                    if (report)
                        report->setText(error.isEmpty() ? QObject::tr("%1 обновлён.").arg(kind) : error);
                });
            });
        });
    }
    tabs->addTab(geo, QObject::tr("Geo и DNS hosts"));
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        auto changed = doc;
        for (auto it = fields.begin(); it != fields.end(); ++it)
            changed[it.key()] = it.value()->text().trimmed();
        changed["GlobalProxy"] = global->isChecked();
        changed["FakeDNS"] = fake->isChecked();
        changed["RouteOrder"] = order->currentText();
        changed["DomainStrategy"] = strategy->currentText();
        for (auto it = lists.begin(); it != lists.end(); ++it) {
            QJsonArray values;
            for (const auto &line : it.value()->toPlainText().split('\n'))
                if (!line.trimmed().isEmpty())
                    values.append(line.trimmed());
            changed[it.key()] = values;
        }
        for (const auto &prefix : {QStringLiteral("Remote"), QStringLiteral("Domestic")})
            changed[prefix + "DNSType"] = general->findChild<QComboBox *>(prefix + "DNSType")->currentText();
        QJsonParseError parseError;
        const auto hostDoc = QJsonDocument::fromJson(hosts->toPlainText().toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !hostDoc.isObject()) {
            QMessageBox::warning(&dialog, QObject::tr("DNS hosts"), QObject::tr("Введите JSON-объект доменов и IP."));
            return;
        }
        changed["DnsHosts"] = hostDoc.object();
        PR::Command command;
        QString error;
        if (!PR::Parse(QJsonDocument(changed).toJson(QJsonDocument::Compact), command, error, provider)) {
            QMessageBox::warning(&dialog, QObject::tr("Маршрутизация"), error);
            return;
        }
        doc = changed;
        dialog.accept();
    });
    return dialog.exec() == QDialog::Accepted;
}
} // namespace
bool EditProviderRouting(Configs::SubscriptionOptions &options, QWidget *parent) {
    auto changed = options;
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Маршрутизация подписки"));
    dialog.resize(700, 470);
    auto layout = new QVBoxLayout(&dialog);
    auto enable = new QCheckBox(QObject::tr("Использовать маршрутизацию этой подписки"));
    enable->setChecked(changed.provider_routing_enabled);
    layout->addWidget(enable);
    auto ignore = new QCheckBox(QObject::tr("Не импортировать маршрутизацию при обновлении подписки"));
    ignore->setChecked(changed.provider_ignore_import);
    layout->addWidget(ignore);
    auto note = new QLabel(QObject::tr("Happ / Incy. Эти настройки применяются к серверам этой подписки. Для полного "
                                       "Xray JSON сохраняется встроенная маршрутизация."));
    note->setWordWrap(true);
    layout->addWidget(note);
    auto list = new QListWidget;
    layout->addWidget(list);
    auto profiles = QJsonDocument::fromJson(changed.provider_routing_profiles.toUtf8()).array();
    const auto reload = [&] {
        list->clear();
        list->addItem(QObject::tr("Без маршрутизации подписки (весь трафик через прокси)"));
        const auto selected =
            QJsonDocument::fromJson(changed.provider_routing.toUtf8()).object().value("Name").toString();
        for (const auto &value : profiles) {
            const auto name = value.toObject().value("Name").toString(QObject::tr("Без названия"));
            list->addItem(name);
            if (name == selected)
                list->setCurrentRow(list->count() - 1);
        }
        if (changed.provider_routing == "off" || list->currentRow() < 0)
            list->setCurrentRow(0);
    };
    reload();
    auto actions = new QHBoxLayout;
    layout->addLayout(actions);
    auto add = new QPushButton(QObject::tr("Из буфера"));
    auto edit = new QPushButton(QObject::tr("Изменить"));
    auto copy = new QPushButton(QObject::tr("Копировать"));
    auto remove = new QPushButton(QObject::tr("Удалить"));
    for (auto button : {add, edit, copy, remove})
        actions->addWidget(button);
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] {
        PR::Command command;
        QString error;
        if (!PR::Parse(QApplication::clipboard()->text(), command, error)) {
            QMessageBox::warning(&dialog, QObject::tr("Маршрутизация"), error);
            return;
        }
        changed.provider_ignore_import = false;
        PR::Apply(changed, {command});
        changed.provider_ignore_import = ignore->isChecked();
        profiles = QJsonDocument::fromJson(changed.provider_routing_profiles.toUtf8()).array();
        reload();
    });
    const auto editSelected = [&] {
        const auto index = list->currentRow() - 1;
        if (index < 0 || index >= profiles.size())
            return;
        auto doc = profiles[index].toObject();
        if (editDocument(doc, changed.routing_provider.isEmpty() ? "Happ" : changed.routing_provider, &dialog)) {
            profiles[index] = doc;
            changed.provider_routing = QJsonDocument(doc).toJson(QJsonDocument::Compact);
            reload();
        }
    };
    QObject::connect(edit, &QPushButton::clicked, &dialog, editSelected);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, [&](QListWidgetItem *) { editSelected(); });
    QObject::connect(copy, &QPushButton::clicked, &dialog, [&] {
        const auto index = list->currentRow() - 1;
        if (index < 0 || index >= profiles.size())
            return;
        QApplication::clipboard()->setText(
            (changed.routing_provider.isEmpty() ? "happ" : changed.routing_provider.toLower()) + "://routing/add/" +
            QString::fromLatin1(QJsonDocument(profiles[index].toObject())
                                    .toJson(QJsonDocument::Compact)
                                    .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        const auto index = list->currentRow() - 1;
        if (index < 0 || index >= profiles.size())
            return;
        profiles.removeAt(index);
        changed.provider_routing = "off";
        reload();
    });
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    changed.provider_routing_enabled = enable->isChecked();
    changed.provider_ignore_import = ignore->isChecked();
    const auto index = list->currentRow() - 1;
    changed.provider_routing =
        index >= 0 && index < profiles.size()
            ? QString::fromUtf8(QJsonDocument(profiles[index].toObject()).toJson(QJsonDocument::Compact))
            : "off";
    changed.provider_routing_profiles = QJsonDocument(profiles).toJson(QJsonDocument::Compact);
    if (changed.routing_provider.isEmpty() && !profiles.isEmpty())
        changed.routing_provider = "Happ";
    options = changed;
    return true;
}

bool SaveProviderRoutingPreviews(const QString &directory) {
    if (directory.isEmpty())
        return true;
    QDir().mkpath(directory);
    const QJsonObject document{{"Name", "Happ Default"},
                               {"GlobalProxy", true},
                               {"DirectSites", QJsonArray{"geosite:WHITELIST", "domain:example.com"}},
                               {"ProxySites", QJsonArray{"domain:example.net"}},
                               {"BlockSites", QJsonArray{"domain:ads.example.org"}},
                               {"RemoteDNSType", "DoH"},
                               {"RemoteDNSDomain", "https://cloudflare-dns.com/dns-query"},
                               {"RemoteDNSIP", "1.1.1.1"},
                               {"DomesticDNSIP", "8.8.8.8"}};
    Configs::SubscriptionOptions options;
    PR::Apply(options, {PR::Command{"Happ", true, false, document}});
    bool saved = false;
    QTimer::singleShot(100, [&, directory] {
        if (auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
            saved = dialog->grab().save(directory + "/provider-routing-list.png");
            dialog->reject();
        }
    });
    EditProviderRouting(options, nullptr);
    if (!saved)
        return false;
    auto editable = document;
    QTimer::singleShot(100, [&, directory] {
        if (auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
            auto tabs = dialog->findChild<QTabWidget *>();
            saved = dialog->grab().save(directory + "/provider-routing-general.png");
            tabs->setCurrentIndex(1);
            QApplication::processEvents();
            saved = saved && dialog->grab().save(directory + "/provider-routing-rules.png");
            tabs->setCurrentIndex(4);
            QApplication::processEvents();
            saved = saved && dialog->grab().save(directory + "/provider-routing-geo.png");
            dialog->reject();
        }
    });
    editDocument(editable, "Happ", nullptr);
    return saved;
}
