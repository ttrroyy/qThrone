#include "include/ui/setting/dialog_integration.h"

#include <include/global/GuiUtils.hpp>

#include "include/api/remote/Server.hpp"
#include "include/global/LocalNetwork.hpp"
#include "include/ui/mainwindow_interface.h"
#include <QAction>
#include <QClipboard>
#include <QHostAddress>
#include <QLabel>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScreen>

DialogIntegration::DialogIntegration(QWidget *parent, const QList<QAction*>& actions) : QDialog(parent), ui(new Ui::DialogIntegration) {
    ui->setupUi(this);
    ui->show_mainwindow->setKeySequence(Configs::dataManager->settingsRepo->hotkey_mainwindow);
    ui->show_groups->setKeySequence(Configs::dataManager->settingsRepo->hotkey_group);
    ui->show_routes->setKeySequence(Configs::dataManager->settingsRepo->hotkey_route);
    ui->system_proxy->setKeySequence(Configs::dataManager->settingsRepo->hotkey_system_proxy_menu);
    ui->toggle_proxy->setKeySequence(Configs::dataManager->settingsRepo->hotkey_toggle_system_proxy);
    ui->toggle_connection->setKeySequence(Configs::dataManager->settingsRepo->hotkey_toggle_connection);
    ui->toggle_tun->setKeySequence(Configs::dataManager->settingsRepo->hotkey_toggle_tun);

    generateShortcutItems(actions);

    GetMainWindow()->RegisterHotkey(true);
    if (!GetMainWindow()->IsGlobalHotkeySupported()) {
        auto note = new QLabel(QCoreApplication::translate("GlobalHotkeys", "Global hotkeys are not supported in this desktop session."), ui->tab);
        note->setWordWrap(true);
        ui->formLayout->insertRow(0, note);
    }

    setupApiTab();

    // Sized with the LAN warning shown, so selecting "Local network" later never needs a taller window.
    ui->api_lan_warning->setVisible(true);
    const auto *scr = screen() != nullptr ? screen() : QGuiApplication::primaryScreen();
    if (scr != nullptr) resize(sizeHint().boundedTo(scr->availableGeometry().size()));
    updateApiControls();
}

void DialogIntegration::generateShortcutItems(const QList<QAction*>& actions)
{
    auto widget = new QWidget(ui->shortcut_area);
    auto layout = new QFormLayout(widget);
    widget->setLayout(layout);
    ui->shortcut_area->setWidget(widget);
    for (auto action : actions)
    {
        auto kseq = new QtExtKeySequenceEdit(this);
        if (!action->shortcut().isEmpty()) kseq->setKeySequence(action->shortcut());
        seqEdit2ID[kseq] = action->data().toString();
        layout->addRow(action->text(), kseq);
    }
}

void DialogIntegration::setupApiTab()
{
    const auto &settings = Configs::dataManager->settingsRepo;
    const auto key = RemoteApi::KeyFromText(settings->remote_api_key);
    savedApiKey = key.isEmpty() ? QString() : RemoteApi::KeyToText(key);
    ui->api_enable->setChecked(settings->remote_api_enable);
    ui->api_listen->setCurrentIndex(settings->remote_api_lan ? 1 : 0);
    ui->api_port->setValue(IsValidPort(settings->remote_api_port) ? settings->remote_api_port : RemoteApi::kDefaultPort);
    ui->api_allow->setText(settings->remote_api_allow);
    ui->api_key->setText(savedApiKey);
    if (ui->api_enable->isChecked() && savedApiKey.isEmpty()) ui->api_key->setText(RemoteApi::KeyToText(RemoteApi::GenerateKey()));
    ui->api_help->setText(tr("Scripts connect with thronectl or the qThrone API library.").toHtmlEscaped()
                          + QStringLiteral("<br><a href=\"https://github.com/throneproj/Throne-api\">https://github.com/throneproj/Throne-api</a>"));

    auto *server = RemoteApi::Server::instance();
    ui->api_status->setText(server->statusText());
    connect(server, &RemoteApi::Server::statusChanged, this, [this](const QString &text) { ui->api_status->setText(text); });

    connect(ui->api_enable, &QCheckBox::toggled, this, [this](bool on) {
        if (on && ui->api_key->text().isEmpty()) ui->api_key->setText(RemoteApi::KeyToText(RemoteApi::GenerateKey()));
        updateApiControls();
    });
    connect(ui->api_listen, &QComboBox::currentIndexChanged, this, [this] { updateApiControls(); });
    connect(ui->api_key_show, &QPushButton::toggled, this, [this](bool on) {
        ui->api_key->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
        ui->api_key_show->setText(on ? tr("Hide") : tr("Show"));
    });
    connect(ui->api_key_copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(ui->api_key->text()); });
    connect(ui->api_key_regenerate, &QPushButton::clicked, this, [this] { regenerateApiKey(); });
    connect(ui->api_copy_env, &QPushButton::clicked, this, [this] { copyForScripts(); });
}

void DialogIntegration::updateApiControls()
{
    const bool enabled = ui->api_enable->isChecked();
    const bool lan = ui->api_listen->currentIndex() == 1;
    for (auto *widget : std::initializer_list<QWidget *>{ui->api_listen_l, ui->api_listen, ui->api_port_l, ui->api_port, ui->api_key_l, ui->api_key,
                                                         ui->api_key_show, ui->api_key_copy, ui->api_key_regenerate, ui->api_copy_env}) {
        widget->setEnabled(enabled);
    }
    ui->api_allow_l->setEnabled(enabled && lan);
    ui->api_allow->setEnabled(enabled && lan);
    ui->api_lan_warning->setVisible(enabled && lan);
}

void DialogIntegration::regenerateApiKey()
{
    if (!savedApiKey.isEmpty() && ui->api_key->text() == savedApiKey) {
        QMessageBox box(QMessageBox::Question, tr("Regenerate Key"), tr("Generate a new key?"), QMessageBox::Yes | QMessageBox::Cancel, this);
        box.setInformativeText(tr("Scripts using the current key will stop working."));
        if (box.exec() != QMessageBox::Yes) return;
    }
    ui->api_key->setText(RemoteApi::KeyToText(RemoteApi::GenerateKey()));
}

void DialogIntegration::copyForScripts()
{
    auto host = QStringLiteral("127.0.0.1");
    if (ui->api_listen->currentIndex() == 1) {
        if (const auto lan = LocalNetwork::LanAddress(); !lan.isEmpty()) host = lan.contains(':') ? "[" + lan + "]" : lan;
    }
    QApplication::clipboard()->setText(QStringLiteral("THRONE_ADDR=%1:%2\nTHRONE_KEY=%3").arg(host, QString::number(ui->api_port->value()), ui->api_key->text()));
}

bool DialogIntegration::isValidAllowEntry(const QString &entry)
{
    const auto host = entry.section('/', 0, 0);
    // parseSubnet alone takes abbreviated IPv4 such as "10" for 10.0.0.0/8.
    if (!host.contains(':') && host.count('.') != 3) return false;
    const auto subnet = entry.contains('/') ? entry : entry + (host.contains(':') ? "/128" : "/32");
    return QHostAddress::parseSubnet(subnet).second >= 0;
}

void DialogIntegration::accept()
{
    QStringList invalid;
    if (ui->api_allow->isEnabled()) {
        static const QRegularExpression separators(QStringLiteral("[,\\s]+"));
        for (const auto &entry : ui->api_allow->text().split(separators, Qt::SkipEmptyParts)) {
            if (!isValidAllowEntry(entry)) invalid << entry;
        }
    }
    if (!invalid.isEmpty()) {
        ui->tabWidget->setCurrentWidget(ui->tab_api);
        ui->api_allow->setFocus();
        QMessageBox box(QMessageBox::Warning, tr("Allowed addresses"), tr("These entries are not IP addresses or CIDR ranges:") + "\n\n" + invalid.join('\n'), QMessageBox::Ok, this);
        box.setTextFormat(Qt::PlainText);
        box.exec();
        return;
    }

    const auto &settings = Configs::dataManager->settingsRepo;
    settings->hotkey_mainwindow = ui->show_mainwindow->keySequence().toString();
    settings->hotkey_group = ui->show_groups->keySequence().toString();
    settings->hotkey_route = ui->show_routes->keySequence().toString();
    settings->hotkey_system_proxy_menu = ui->system_proxy->keySequence().toString();
    settings->hotkey_toggle_system_proxy = ui->toggle_proxy->keySequence().toString();
    settings->hotkey_toggle_connection = ui->toggle_connection->keySequence().toString();
    settings->hotkey_toggle_tun = ui->toggle_tun->keySequence().toString();

    auto mp = seqEdit2ID.toStdMap();
    for (const auto& [kseq, actionID] : mp)
    {
        settings->shortcuts[actionID] = kseq->keySequence();
    }

    const bool apiEnable = ui->api_enable->isChecked();
    const bool apiLan = ui->api_listen->currentIndex() == 1;
    const int apiPort = ui->api_port->value();
    const auto apiKey = ui->api_key->text();
    const auto apiAllow = ui->api_allow->text().trimmed();
    const bool apiChanged = apiEnable != settings->remote_api_enable || apiLan != settings->remote_api_lan || apiPort != settings->remote_api_port
                            || apiKey != settings->remote_api_key || apiAllow != settings->remote_api_allow;
    settings->remote_api_enable = apiEnable;
    settings->remote_api_lan = apiLan;
    settings->remote_api_port = apiPort;
    settings->remote_api_key = apiKey;
    settings->remote_api_allow = apiAllow;
    settings->Save();

    MW_dialog_message(MwMessage::UpdateShortcuts, {});
    if (apiChanged) MW_dialog_message(MwMessage::UpdateSettings, {MwArg::RemoteApi});
    const auto failures = GetMainWindow()->RegisterHotkey(false);
    if (!failures.isEmpty() && GetMainWindow()->IsGlobalHotkeySupported()) {
        QMessageBox box(QMessageBox::Warning, tr("Hotkey"), tr("These global hotkeys could not be registered:") + "\n\n" + failures.join('\n'), QMessageBox::Ok, this);
        box.setTextFormat(Qt::PlainText);
        box.exec();
    }
    QDialog::accept();
}

void DialogIntegration::reject()
{
    GetMainWindow()->RegisterHotkey(false);
    QDialog::reject();
}

DialogIntegration::~DialogIntegration() {
    delete ui;
}
