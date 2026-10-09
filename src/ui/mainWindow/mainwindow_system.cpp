#include "include/ui/mainwindow.h"
#include "NkrVersion.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QThread>

#include "include/api/RPC.h"
#include "include/api/remote/Server.hpp"
#include "include/configs/generate.h"
#include "include/global/Configs.hpp"
#include "include/global/HTTPRequestHelper.hpp"
#include "include/global/LocalNetwork.hpp"
#include "include/global/Logger.hpp"
#include "include/scanner/ScanManager.h"
#include "include/sys/KillSwitch.hpp"
#include "include/sys/Process.hpp"
#include "include/sys/SystemProxy.hpp"
#include "include/ui/mainWindow/MainWindowInternal.h"

#include "include/ui/group/dialog_manage_groups.h"
#include "include/ui/setting/dialog_basic_settings.h"
#include "include/ui/setting/dialog_integration.h"
#include "include/ui/setting/dialog_manage_routes.h"
#include "include/ui/setting/dialog_otp_manager.h"
#include "include/ui/setting/dialog_preset_settings.h"
#include "include/ui/setting/dialog_vpn_settings.h"

#ifdef Q_OS_WIN
#include "3rdparty/WinCommander.hpp"
#include "include/sys/windows/WinVersion.h"
#endif
#ifdef Q_OS_LINUX
#include "include/sys/linux/LinuxCap.h"
#endif
#ifdef Q_OS_MACOS
#include "include/sys/macos/MacOS.h"
#endif

qint64 MainWindow::GetCorePid() {
    QMutexLocker lock(&coreProcessMutex);
    return core_process ? core_process->processId() : 0;
}

QString MainWindow::GetRunningConfigName() {
    auto ent = running;
    if (ent == nullptr || ent->outbound == nullptr) return {};
    return ent->outbound->DisplayTypeAndName();
}

void MainWindow::on_menu_basic_settings_triggered() {
    USE_DIALOG(DialogBasicSettings)
}

void MainWindow::on_menu_manage_groups_triggered() {
    USE_DIALOG(DialogManageGroups)
}

void MainWindow::on_menu_routing_settings_triggered() {
    if (dialog_is_using) return;
    dialog_is_using = true;
    auto dialog = new DialogManageRoutes(this);
    connect(dialog, &QDialog::finished, this, [=,this] {
        dialog->deleteLater();
        dialog_is_using = false;
    });
    dialog->show();
}

void MainWindow::on_menu_vpn_settings_triggered() {
    USE_DIALOG(DialogVPNSettings)
}

void MainWindow::on_menu_preset_settings_triggered() {
    USE_DIALOG(DialogPresetSettings)
}

void MainWindow::on_menu_otp_manager_triggered() {
    USE_DIALOG(DialogOtpManager)
}

void MainWindow::on_menu_integration_settings_triggered() {
    if (dialog_is_using) return;
    dialog_is_using = true;
    auto dialog = new DialogIntegration(this, getActionsForShortcut());
    connect(dialog, &QDialog::finished, this, [=,this]
    {
        dialog->deleteLater();
        dialog_is_using = false;
    });
    dialog->show();
}

void MainWindow::on_commitDataRequest() {
    qDebug() << "Start of data save";

    auto* settings = Configs::dataManager->settingsRepo.get();

    settings->mainWindowGeometry = this->saveGeometry().toBase64(QByteArray::Base64Encoding);
    if (!isMaximized()) {
        auto news = QString("%1x%2").arg(size().width()).arg(size().height());
        if (settings->mw_size != news) settings->mw_size = news;
    }
    settings->splitter_state = ui->splitter->saveState().toBase64();

    // Backstop only: this runs on a graceful exit, so it must never be the sole write.
    if (settings->remember_enable && settings->started_id >= 0) settings->remember_id = settings->started_id;
    settings->remember_system_proxy = settings->spmode_system_proxy;
    settings->remember_tun = settings->spmode_vpn;

    settings->Save();
    qDebug() << "End of data save";
}

void MainWindow::prepare_exit()
{
    qDebug() << "prepare for exit...";
    mu_exit.lock();
    if (Configs::dataManager->settingsRepo->prepare_exit)
    {
        qDebug() << "prepare exit had already succeeded, ignoring...";
        mu_exit.unlock();
        return;
    }
    Configs::dataManager->settingsRepo->prepare_exit = true;
    LOG_INFO("prepare_exit started, tearing down proxy/tun/core");
    // Before the waits below: their nested event loops would dispatch API requests mid-teardown.
    RemoteApi::Server::instance()->shutdown();
    // Unconditional: an uncheck may still have its clear queued.
    set_system_proxy(false, true);
    RegisterHiddenMenuShortcuts(true);
    RegisterHotkey(true);
    on_commitDataRequest();
    Scanner::ScanManager::instance()->StopAll(true);
    Configs::dataManager->settingsRepo->noSave = true; // don't change Configs::dataManager->settingsRepo after this line
    profile_stop(false, true);

    runOnThread([=, this]()
    {
        core_process->Kill();
    }, DS_cores, true);
    HideWindow(this);
    tray->hide();
    // Only once the core is gone: until then the guard keeps blocking whatever would leak around the dying tunnel.
    Sys::KillSwitch::instance()->shutdown();

    mu_exit.unlock();
    qDebug() << "prepare exit done!";
}

void MainWindow::on_menu_exit_triggered() {
    const bool restart = exit_reason == ExitReason::Restart || exit_reason == ExitReason::RestartWithTun ||
                         exit_reason == ExitReason::RestartElevated;
    const auto program = QApplication::applicationFilePath();
    QStringList arguments;
    if (restart) {
        arguments = Configs::dataManager->settingsRepo->argv;
        if (arguments.length() > 0) {
            arguments.removeFirst();
            arguments.removeAll("-tray");
            arguments.removeAll("-flag_restart_tun_on");
        }
        if (exit_reason == ExitReason::RestartWithTun) arguments << "-flag_restart_tun_on";
    }

    bool relaunched = false;
#ifdef Q_OS_WIN
    if (exit_reason == ExitReason::RestartWithTun || exit_reason == ExitReason::RestartElevated) {
        // Asked before the teardown so a declined UAC prompt leaves this instance running; the new one waits for this pid to exit.
        // The UAC wait pumps messages with the UI still live, so a second exit request must not launch a second instance.
        static bool elevating = false;
        if (elevating) return;
        elevating = true;
        const auto elevatedArguments = arguments + QStringList{QString("-wait_pid=%1").arg(QCoreApplication::applicationPid())};
        const bool launched = WinCommander::runProcessElevated(program, elevatedArguments, QApplication::applicationDirPath(), 1, false) == 0;
        elevating = false;
        if (!launched) {
            exit_reason = ExitReason::None;
            MW_show_log(tr("Restart as administrator was cancelled, Throne keeps running"));
            return;
        }
        relaunched = true;
    }
#endif

    prepare_exit();
    if (exit_reason == ExitReason::RunUpdater) {
        QDir::setCurrent(QApplication::applicationDirPath());
#ifdef Q_OS_WIN
        QFile::remove("./qThroneUpdater.old");
        QFile::copy("./qThroneUpdater.exe", "./qThroneUpdater.old");
        QProcess::startDetached("./qThroneUpdater.old", QStringList{QString::number(QCoreApplication::applicationPid())});
#else
        QProcess::startDetached("./qThroneUpdater", QStringList{QString::number(QCoreApplication::applicationPid())});
#endif
    } else if (restart && !relaunched) {
        QDir::setCurrent(QApplication::applicationDirPath());
        QProcess::startDetached(program, arguments);
    }
    QCoreApplication::quit();
}

void MainWindow::toggle_system_proxy() {
    auto currentState = Configs::dataManager->settingsRepo->spmode_system_proxy;
    if (currentState) {
        set_spmode_system_proxy(false);
    } else {
        set_spmode_system_proxy(true);
    }
}

void MainWindow::toggle_tun() {
    if (m_profileConnecting || m_profileDisconnecting) return;
    set_spmode_vpn(!Configs::dataManager->settingsRepo->spmode_vpn);
}

bool MainWindow::get_elevated_permissions(bool interactive) {
    if (Configs::dataManager->settingsRepo->disable_privilege_req)
    {
        MW_show_log(tr("User opted for no privilege req, some features may not work"));
        return true;
    }
    if (Configs::IsAdmin()) return true;
    if (!interactive) return false;
#ifdef NKR_ELEVATION_HINT
    MessageBoxWarning(software_name, tr("This installation cannot grant the core privileges by itself.") + "\n\n" + NKR_ELEVATION_HINT);
    return false;
#endif
#ifdef Q_OS_LINUX
    if (!Linux_HavePkexec()) {
        MessageBoxWarning(software_name, "Please install \"pkexec\" first.");
        return false;
    }
    auto n = QMessageBox::warning(GetMessageBoxParent(), software_name, tr("Please give the core root privileges"), QMessageBox::Yes | QMessageBox::No);
    if (n == QMessageBox::Yes) {
        runOnNewThread([=,this]
        {
            auto chownArgs = QString("root:root " + Configs::FindCoreRealPath());
            auto ret = Linux_Run_Command("chown", chownArgs);
            if (ret != 0) {
                MW_show_log(QString("Failed to run chown %1 code is %2").arg(chownArgs).arg(ret));
            }
            auto chmodArgs = QString("u+s " + Configs::FindCoreRealPath());
            ret = Linux_Run_Command("chmod", chmodArgs);
            if (ret == 0) {
                StopVPNProcess();
            } else {
                MW_show_log(QString("Failed to run chmod %1").arg(chmodArgs));
            }
        });
        return false;
    }
#endif
#ifdef Q_OS_WIN
    auto n = QMessageBox::warning(GetMessageBoxParent(), software_name, tr("Please run Throne as admin"), QMessageBox::Yes | QMessageBox::No);
    if (n == QMessageBox::Yes) {
        this->exit_reason = ExitReason::RestartWithTun;
        on_menu_exit_triggered();
    }
#endif

#ifdef Q_OS_MACOS
    if (Configs::isSetuidSet(Configs::FindCoreRealPath().toStdString()))
    {
        StopVPNProcess();
        return true;
    }
    auto n = QMessageBox::warning(GetMessageBoxParent(), software_name, tr("Please give the core root privileges"), QMessageBox::Yes | QMessageBox::No);
    if (n == QMessageBox::Yes)
    {
        auto Command = QString("sudo chown root:wheel '%1' && sudo chmod u+s '%1'").arg(Configs::FindCoreRealPath());
        auto ret = Mac_Run_Command(Command);
        if (ret == 0) {
            MessageBoxInfo(tr("Requesting permission"), tr("Please Enter your password in the opened terminal, then try again"));
            return false;
        } else {
            MW_show_log(QString("Failed to run %1 with %2").arg(Command).arg(ret));
            return false;
        }
    }
#endif
    return false;
}

namespace {
    // networksetup and gsettings runs are slow, and profile start/stop call in from their own threads.
    QThread *systemProxyThread() {
        static auto *thread = [] {
            auto *t = new QThread;
            t->start();
            return t;
        }();
        return thread;
    }
}

void MainWindow::set_system_proxy(bool enable, bool wait) {
    const auto &settings = Configs::dataManager->settingsRepo;
    const auto host = LocalNetwork::InboundConnectHost();
    const auto port = settings->inbound_socks_port;
    const auto format = settings->proxy_scheme;
    runOnThread([=] {
        QString error;
        if (!enable) {
            error = SystemProxy_Clear();
        } else if (Configs::dataManager->settingsRepo->spmode_system_proxy) {
            // Rechecked: a profile start can queue this after the box was unchecked.
            error = SystemProxy_Apply(host, port, format);
        }
        if (!error.isEmpty()) MW_show_log(tr("System proxy: %1").arg(error));
    }, systemProxyThread(), wait);
}

void MainWindow::set_spmode_system_proxy(bool enable, bool save) {
    set_spmode_system_proxy(enable, ModeChange{.save = save});
}

bool MainWindow::set_spmode_system_proxy(bool enable, const ModeChange &change) {
    const auto &settings = Configs::dataManager->settingsRepo;
    if (enable && settings->disable_mixed_inbound) {
        if (change.interactive) {
            runOnUiThread([=, this] {
               MessageBoxWarning("Invalid Operation", "Cannot set system proxy when mixed inbound is disabled.");
            });
        }
        ui->checkBox_SystemProxy->setChecked(false);
        return false;
    }
    settings->spmode_system_proxy = enable;
    bool restarted = false;
    if (running) {
        set_system_proxy(enable);
        if (!enable && settings->reset_proxy_on_disable_sp && change.restart) {
            profile_start(StartRequest{running->id, change.interactive, change.restartSerial});
            restarted = true;
        }
    }

    if (change.save) {
        settings->remember_system_proxy = enable;
        settings->Save();
    }

    refresh_status();
    return restarted;
}

void MainWindow::set_spmode_vpn(bool enable, bool save) {
    set_spmode_vpn(enable, ModeChange{.save = save});
}

bool MainWindow::set_spmode_vpn(bool enable, const ModeChange &change) {
    const auto &settings = Configs::dataManager->settingsRepo;
    if (enable == settings->spmode_vpn) return false;

    if (enable && !Configs::IsAdmin() && !get_elevated_permissions(change.interactive)) {
        refresh_status();
        return false;
    }

    if (change.save) {
        // Written here, after the elevation check, so a failed enable is not remembered.
        settings->remember_tun = enable;
        settings->Save();
    }

    settings->spmode_vpn = enable;
    refresh_status();

    if (!change.restart || settings->started_id < 0) return false;
    profile_start(StartRequest{settings->started_id, change.interactive, change.restartSerial});
    return true;
}

bool MainWindow::choose_route(int routeId, bool interactive, quint64 restartSerial) {
    const auto &settings = Configs::dataManager->settingsRepo;
    if (settings->current_route_id == routeId) return false;
    settings->current_route_id = routeId;
    settings->Save();
    if (settings->started_id < 0) return false;
    profile_start(StartRequest{settings->started_id, interactive, restartSerial});
    return true;
}

bool MainWindow::StopVPNProcess() {
    runOnThread([=, this]
    {
        core_process->Kill();
    }, DS_cores, true);

    return true;
}

void MainWindow::RestartCore() {
    runOnThread([=, this]
    {
        profile_stop(true, true, true);
        core_process->Kill();
    }, DS_cores);
}

void MainWindow::kill_switch_state_changed() {
    const auto state = Sys::KillSwitch::instance()->state();
    const bool failed = state == Sys::KillSwitch::State::Failed;
    const bool armed = state == Sys::KillSwitch::State::Armed;
    const bool enteredFailed = failed && !m_killSwitchWasFailed;
    const bool enteredArmed = armed && !m_killSwitchWasArmed;
    m_killSwitchWasFailed = failed;
    m_killSwitchWasArmed = armed;

    refresh_status();
    if (!failed && m_killSwitchDialog) m_killSwitchDialog->close();
    if (enteredFailed) show_kill_switch_problem();
    if (enteredArmed && !guard_core_restart_pending() && core_lacks_guard_identity()) {
        restart_core_for_guard(StartRequest{Configs::dataManager->settingsRepo->started_id});
    }
}

void MainWindow::show_kill_switch_problem() {
    auto *killSwitch = Sys::KillSwitch::instance();
    if (killSwitch->state() != Sys::KillSwitch::State::Failed) return;
    const bool privilege = killSwitch->failedForPrivileges();
    if (m_killSwitchDialog) {
        if (m_killSwitchDialog->property("privilege").toBool() == privilege) {
            m_killSwitchDialog->setText(killSwitch->failureText());
            return;
        }
        m_killSwitchDialog->close();
    }

    auto *box = new QMessageBox(QMessageBox::Warning, tr("Kill switch is not active"), killSwitch->failureText(),
                                QMessageBox::NoButton, this);
    box->setTextFormat(Qt::PlainText);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setProperty("privilege", privilege);
#ifdef Q_OS_WIN
    auto *fix = box->addButton(privilege ? tr("Restart as Administrator") : tr("Retry"), QMessageBox::AcceptRole);
#else
    auto *fix = box->addButton(privilege ? tr("Grant Privileges") : tr("Retry"), QMessageBox::AcceptRole);
#endif
    auto *disable = box->addButton(tr("Disable Kill Switch"), QMessageBox::DestructiveRole);
    auto *cancel = box->addButton(QMessageBox::Cancel);
    box->setDefaultButton(fix);
    box->setEscapeButton(cancel);
    m_killSwitchDialog = box;

    // Custom-button result codes changed in Qt 6.5, so only the clicked button pointer is trusted.
    connect(box, &QMessageBox::finished, this, [this, box, fix, disable, privilege] {
        const auto *clicked = box->clickedButton();
        if (m_killSwitchDialog == box) m_killSwitchDialog = nullptr;
        if (clicked == disable) {
            disable_kill_switch();
            return;
        }
        if (clicked != fix) return;
        if (!privilege) {
            Sys::KillSwitch::instance()->apply();
            return;
        }
#ifdef Q_OS_WIN
        // Queued: ShellExecuteEx's UAC pump runs this box's deleteLater, so exiting from here frees it under QMessageBox (#1961).
        QMetaObject::invokeMethod(this, [this] {
            exit_reason = ExitReason::RestartElevated;
            on_menu_exit_triggered();
        }, Qt::QueuedConnection);
#else
        // Otherwise the core restarts once it is privileged, and CoreStarted retries.
        if (get_elevated_permissions()) Sys::KillSwitch::instance()->apply();
#endif
    });
    box->open();
}

void MainWindow::show_startstop_menu() {
    if (!Configs::dataManager->settingsRepo->kill_switch) return;
    QMenu menu(this);
    connect(menu.addAction(tr("Disable Kill Switch")), &QAction::triggered, this, [this] { confirm_disable_kill_switch(); });
    auto *button = ui->toolButton_startstop;
    menu.exec(button->mapToGlobal(QPoint(0, button->height())));
}

void MainWindow::confirm_disable_kill_switch() {
    auto *box = new QMessageBox(QMessageBox::Warning, tr("Disable kill switch"),
                                tr("Turn off the kill switch? Traffic that does not go through Throne will no longer be blocked."),
                                QMessageBox::NoButton, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    auto *disable = box->addButton(tr("Disable Kill Switch"), QMessageBox::DestructiveRole);
    auto *cancel = box->addButton(QMessageBox::Cancel);
    // Cancel is the default, so a stray Enter cannot complete the two-step disable.
    box->setDefaultButton(cancel);
    box->setEscapeButton(cancel);
    connect(box, &QMessageBox::finished, this, [this, box, disable] {
        if (box->clickedButton() == disable) disable_kill_switch();
    });
    box->open();
}

void MainWindow::disable_kill_switch() {
    Configs::dataManager->settingsRepo->kill_switch = false;
    Configs::dataManager->settingsRepo->Save();
    Sys::KillSwitch::instance()->apply();
}

bool MainWindow::core_lacks_guard_identity() {
#ifdef Q_OS_WIN
    return false;
#else
    QMutexLocker lock(&coreProcessMutex);
    return core_process != nullptr && Configs::dataManager->settingsRepo->core_running && !core_process->guard_identity;
#endif
}

bool MainWindow::guard_core_restart_pending() const {
    constexpr qint64 kRestartWindowMs = 15000;
    return m_guardCoreRestart.isValid() && !m_guardCoreRestart.hasExpired(kRestartWindowMs);
}

void MainWindow::restart_core_for_guard(const StartRequest &request) {
    bool haveCore;
    {
        // The IPC handler takes this slot under the same lock when the new core connects.
        QMutexLocker lock(&coreProcessMutex);
        haveCore = core_process != nullptr;
        if (haveCore) core_process->start_profile_when_core_is_up = request.profileId;
    }
    if (!haveCore) {
        emit start_finished(request.serial, request.profileId, StartOutcome::CoreUnavailable, {});
        return;
    }
    defer_start_to_core(request);
    if (guard_core_restart_pending()) return;
    m_guardCoreRestart.start();
    MW_show_log(tr("[Kill switch] Restarting the core so that its own traffic passes the kill switch..."));
    runOnThread([this] {
        profile_stop(true, true);
        core_process->Restart();
    }, DS_cores);
}

namespace {

bool isNewer(QString assetName) {
    if (QString(NKR_VERSION).isEmpty()) return false;
    assetName = assetName.mid(assetName.indexOf('-') + 1); // release product prefix
    if (assetName.count('-') < 1) return false;
    QString version;
    auto spl = assetName.split('-');
    version += spl[0];
    if (spl[1].contains("beta") || spl[1].contains("alpha") || spl[1].contains("rc")) version += "."+spl[1];
    auto parts = version.split("."); // [1,2,3,beta,13]
    auto currentParts = QString(NKR_VERSION).replace("-", ".").split('.');
    if (parts.size() < 3 || currentParts.size() < 3)
    {
        MW_show_log("Version strings seem to be invalid" + QString(NKR_VERSION) + " and " + version);
        return false;
    }
    std::vector<int> verNums;
    std::vector<int> currNums;
    verNums.push_back(parts[0].toInt());
    verNums.push_back(parts[1].toInt());
    verNums.push_back(parts[2].toInt());
    if (parts.size() > 3)
    {
        if (parts[3] == "alpha") verNums.push_back(1);
        if (parts[3] == "beta") verNums.push_back(2);
        if (parts[3] == "rc") verNums.push_back(3);
        if (parts.size() > 4) verNums.push_back(parts[4].toInt());
    }

    currNums.push_back(currentParts[0].toInt());
    currNums.push_back(currentParts[1].toInt());
    currNums.push_back(currentParts[2].toInt());
    if (currentParts.size() > 3)
    {
        if (currentParts[3] == "alpha") currNums.push_back(1);
        if (currentParts[3] == "beta") currNums.push_back(2);
        if (currentParts[3] == "rc") currNums.push_back(3);
        if (currentParts.size() > 4) currNums.push_back(currentParts[4].toInt());
    }

    if (verNums.size() < 3 || currNums.size() < 3)
    {
        MW_show_log("Version strings seem to be invalid" + QString(NKR_VERSION) + " and " + version);
        return false;
    }

    for (int i=0;i<3;i++)
    {
        if (verNums[i] > currNums[i]) return true;
        if (verNums[i] < currNums[i]) return false;
    }

    if (verNums.size() == 5 && currNums.size() == 3) return false;
    if (verNums.size() == 3 && currNums.size() == 5) return true;
    if (verNums.size() == 5 && currNums.size() == 5)
    {
        for (int i=3;i<5;i++)
        {
            if (verNums[i] > currNums[i]) return true;
            if (verNums[i] < currNums[i]) return false;
        }
    } else
    {
		MW_show_log("There are no updates. You have the latest version - " + QString(NKR_VERSION));
        return false;
    }
    return false;
}

constexpr auto dashboardDownloadURL = "https://github.com/SagerNet/sing-box-dashboard/archive/refs/heads/gh-pages.zip";

bool copyOut(const QString &from, const QString &to) {
    QFile::remove(to);
    if (!QFile::copy(from, to)) return false;
    // Resource files are read-only, and QFile::copy carries that onto the copy.
    return QFile::setPermissions(to, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                     QFileDevice::ReadGroup | QFileDevice::ReadOther);
}

bool unpackBundledDashboard(const QDir &dest) {
    if (!QFile::exists(":/dashboard/index.html")) return false;
    const QDir bundle(":/dashboard");
    QDirIterator it(bundle.path(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const auto source = it.next();
        const auto target = dest.filePath(bundle.relativeFilePath(source));
        if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !copyOut(source, target)) return false;
    }
    return true;
}

} // namespace

void MainWindow::SeedDashboard() {
    QDir dashDir(Configs::apiDashboardDir);
    if (!dashDir.exists() && !QDir().mkpath(Configs::apiDashboardDir)) return;
    if (!QFile::exists(dashDir.filePath("index.html"))) unpackBundledDashboard(dashDir);
    // Reinstalling replaces the whole directory, so this cannot be a one-time copy.
    auto src = QFile(":/Throne/dashboard-bootstrap.html");
    if (!src.open(QIODevice::ReadOnly)) return;
    const auto data = src.readAll();
    src.close();
    if (auto dest = QFile(dashDir.filePath("throne.html")); dest.open(QIODevice::Truncate | QIODevice::WriteOnly)) {
        dest.write(data);
        dest.close();
    }
}

void MainWindow::OpenDashboard() {
    const auto &settings = *Configs::dataManager->settingsRepo;
    const auto port = settings.core_box_api_port;
    if (port <= 0) {
        MessageBoxWarning(software_name, tr("The sing-box API is disabled. Set a listen port in Preferences > Basic Settings > Core."));
        return;
    }
    if (settings.started_id < 0) {
        MessageBoxWarning(software_name, tr("Start a profile first; the dashboard is served by the running core."));
        return;
    }

    const auto show = [this, port] {
        SeedDashboard();
        // Fragment, not query: browsers never send it to the server.
        QUrl url(QString("http://127.0.0.1:%1/dashboard/throne.html").arg(port));
        url.setFragment(QString("secret=%1&url=127.0.0.1:%2")
                            .arg(QString::fromUtf8(QUrl::toPercentEncoding(Configs::dataManager->settingsRepo->core_box_api_secret)))
                            .arg(port),
                        QUrl::StrictMode);
        QDesktopServices::openUrl(url);
    };

    SeedDashboard();
    if (QFile::exists(QDir(Configs::apiDashboardDir).filePath("index.html"))) {
        show();
        return;
    }

    if (QMessageBox::question(this, tr("Web dashboard"),
                              tr("The dashboard is not installed yet. Download it now?"))
        != QMessageBox::StandardButton::Yes) {
        return;
    }

    runOnNewThread([=, this] {
        if (!mu_download_dashboard.tryLock()) {
            runOnUiThread([=, this] {
                MessageBoxWarning(tr("Cannot start"), tr("A dashboard download is already running"));
            });
            return;
        }
        const auto archive = QString("throne-dashboard.zip");
        auto error = NetworkRequestHelper::DownloadAsset(dashboardDownloadURL, archive, true);
        if (error.isEmpty()) {
            bool ok = false;
            error = API::defaultClient->InstallDashboard(&ok, Configs::GetBasePath() + "/" + archive,
                                                         QDir(Configs::apiDashboardDir).absolutePath());
            if (!ok && error.isEmpty()) error = tr("The core did not answer.");
        }
        QFile::remove(Configs::GetBasePath() + "/" + archive);
        mu_download_dashboard.unlock();

        runOnUiThread([=, this] {
            if (!error.isEmpty()) {
                MessageBoxWarning(tr("Failed to install the dashboard"), error);
                return;
            }
            show();
        });
    });
}

void MainWindow::CheckUpdate() {
    QString search;
#ifdef Q_OS_WIN
#  ifdef Q_PROCESSOR_ARM_64
    search = "windows-arm64";
#  else
#    ifdef Q_OS_WIN64
        if (WinVersion::IsBuildNumGreaterOrEqual(BuildNumber::Windows_10_1809))
            search = "windows64";
        else
	        search = "windowslegacy64";
#    else
	    search = "windows32";
#    endif
#  endif
#endif
#ifdef Q_OS_LINUX
#  ifdef Q_PROCESSOR_X86_64
    search = "linux-amd64";
#  else
    search = "linux-arm64";
#  endif
#endif
#ifdef Q_OS_MACOS
#  ifdef Q_PROCESSOR_X86_64
	search = "macos-amd64";
#  else
	search = "macos-arm64";
#  endif
#endif
    if (search.isEmpty()) {
        runOnUiThread([=,this] {
            MessageBoxWarning(QObject::tr("Update"), QObject::tr("Not official support platform"));
        });
        return;
    }

    // Releases carry no checksum or signature, so TLS is all that vouches for the download URL and the archive.
    HttpGetOptions options;
    options.strictTls = true;
    const QString forkRepository = QStringLiteral(QTHRONE_REPOSITORY);
    if (forkRepository.isEmpty()) {
        runOnUiThread([=,this] { MessageBoxInfo(tr("Update"), tr("This qThrone build has no update repository configured.")); });
        return;
    }
    auto resp = NetworkRequestHelper::HttpGet("https://api.github.com/repos/" + forkRepository + "/releases", options);
    if (!resp.error.isEmpty()) {
        runOnUiThread([=,this] {
            MessageBoxWarning(QObject::tr("Update"), QObject::tr("Requesting update error: %1").arg(resp.error + "\n" + resp.data));
        });
        return;
    }

    QString assets_name, release_download_url, release_url, release_note, note_pre_release;
    bool exitFlag = false;
    QJsonArray array = QString2QJsonArray(resp.data);
    for (const QJsonValue value : array) {
        QJsonObject release = value.toObject();
        if (release["prerelease"].toBool() && !Configs::dataManager->settingsRepo->allow_beta_update) continue;
        for (const QJsonValue asset : release["assets"].toArray()) {
            if (asset["name"].toString().contains(search) && asset["name"].toString().section('.', -1) == QString("zip")) {
                note_pre_release = release["prerelease"].toBool() ? " (Pre-release)" : "";
                release_url = release["html_url"].toString();
                release_note = release["body"].toString();
                assets_name = asset["name"].toString();
                release_download_url = asset["browser_download_url"].toString();
                exitFlag = true;
                break;
            }
        }
        if (exitFlag) break;
    }

    if (release_download_url.isEmpty() || !isNewer(assets_name)) {
        runOnUiThread([=,this] {
            MessageBoxInfo(QObject::tr("Update"), QObject::tr("No update"));
        });
        return;
    }

    runOnUiThread([=,this] {
        auto allow_updater = !Configs::dataManager->settingsRepo->flag_use_appdata;
#ifdef Q_OS_MACOS
        allow_updater = false;
#endif
        QMessageBox box(QMessageBox::Question, QObject::tr("Update") + note_pre_release,
                        QObject::tr("Update found: %1\nRelease note:\n%2").arg(assets_name, release_note));
        QAbstractButton *btn1 = nullptr;
        if (allow_updater) {
            btn1 = box.addButton(QObject::tr("Update"), QMessageBox::AcceptRole);
        }
        QAbstractButton *btn2 = box.addButton(QObject::tr("Open in browser"), QMessageBox::AcceptRole);
        box.addButton(QObject::tr("Close"), QMessageBox::RejectRole);
        box.exec();
        if (btn1 == box.clickedButton() && allow_updater) {
            runOnNewThread([=,this] {
                if (!mu_download_update.tryLock()) {
                    runOnUiThread([=,this](){
                        MessageBoxWarning(tr("Cannot start"), tr("Last download request has not finished yet"));
                    });
                    return;
                }
                QString errors;
                if (!release_download_url.isEmpty()) {
                    auto res = NetworkRequestHelper::DownloadAsset(release_download_url, "qThrone.zip", false, true);
                    if (!res.isEmpty()) {
                        errors += res;
                    }
                }
                mu_download_update.unlock();
                runOnUiThread([=,this] {
                    if (errors.isEmpty()) {
                        auto q = QMessageBox::question(nullptr, QObject::tr("Update"),
                                                       QObject::tr("Update is ready, restart to install?"));
                        if (q == QMessageBox::StandardButton::Yes) {
                            this->exit_reason = ExitReason::RunUpdater;
                            on_menu_exit_triggered();
                        }
                    } else {
                        MessageBoxWarning(tr("Failed to download update assets"), errors);
                    }
                });
            });
        } else if (btn2 == box.clickedButton()) {
            QDesktopServices::openUrl(QUrl(release_url));
        }
    });
}
