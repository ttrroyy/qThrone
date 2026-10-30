#include "include/global/Version.hpp"
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
#include "include/engine/EngineHost.h"
#include "include/engine/ThreadCheck.h"
#include "include/engine/app/ExitController.h"
#include "include/engine/connection/ConnectionManager.h"
#include "include/engine/services/Dashboard.h"
#include "include/engine/supervisor/CoreSupervisor.h"
#include "include/global/Configs.hpp"
#include "include/global/HTTPRequestHelper.hpp"
#include "include/global/LocalNetwork.hpp"
#include "include/global/Logger.hpp"
#include "include/scanner/ScanManager.h"
#include "include/sys/HostPresence.hpp"
#include "include/sys/KillSwitch.hpp"
#include "include/sys/SystemProxy.hpp"
#include "include/ui/mainWindow/MainWindowInternal.h"

#include "include/ui/group/dialog_manage_groups.h"
#include "include/ui/setting/dialog_basic_settings.h"
#include "include/ui/setting/dialog_integration.h"
#include "include/ui/setting/dialog_manage_routes.h"
#include "include/ui/setting/dialog_otp_manager.h"
#include "include/ui/setting/dialog_preset_settings.h"
#include "include/ui/setting/dialog_vpn_settings.h"
#include "include/ui/setting/dialog_vk_relay.h"
#include "include/ui/setting/RawRouteItem.h"
#include "include/ui/setting/RouteItem.h"
#include "include/database/RoutesRepo.h"

#ifdef Q_OS_WIN
#include "3rdparty/WinCommander.hpp"
#include "include/sys/windows/WinVersion.h"
#endif

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

void MainWindow::openRouteProfileEditor(int routeId) {
    // Also keeps the routing dialog from saving its stale copy over this edit.
    if (dialog_is_using) return;
    const auto profile = Configs::dataManager->routesRepo->GetRouteProfile(routeId);
    if (profile == nullptr) return;
    dialog_is_using = true;

    const auto onSaved = [this](const std::shared_ptr<Configs::RouteProfile>& chain) {
        if (!Configs::dataManager->routesRepo->Save(chain)) return;
        if (chain->id == Configs::dataManager->settingsRepo->current_route_id)
            connection()->noteRestartNeeded(Engine::RestartReason::Routing);
    };
    QDialog* editor;
    if (profile->isRaw) {
        auto* raw = new RawRouteItem(this, profile);
        connect(raw, &RawRouteItem::settingsChanged, this, onSaved);
        editor = raw;
    } else {
        auto* item = new RouteItem(this, profile);
        connect(item, &RouteItem::settingsChanged, this, onSaved);
        editor = item;
    }
    connect(editor, &QDialog::finished, this, [=, this] {
        editor->deleteLater();
        dialog_is_using = false;
    });
    editor->setWindowModality(Qt::ApplicationModal);
    editor->show();
}

void MainWindow::on_menu_vpn_settings_triggered() {
    USE_DIALOG(DialogVPNSettings)
}
void MainWindow::on_menu_vk_relay_settings_triggered() {
    USE_DIALOG(DialogVKRelay)
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

void MainWindow::saveWindowState() {
    auto* settings = Configs::dataManager->settingsRepo.get();

    settings->mainWindowGeometry = this->saveGeometry().toBase64(QByteArray::Base64Encoding);
    if (!isMaximized()) {
        auto news = QString("%1x%2").arg(size().width()).arg(size().height());
        if (settings->mw_size != news) settings->mw_size = news;
    }
    settings->splitter_state = ui->splitter->saveState().toBase64();
}

void MainWindow::on_commitDataRequest() {
    saveWindowState();
    exitController()->persistSessionState();
}

void MainWindow::on_menu_exit_triggered() {
    // A teardown already running quits on its own; a second request would cut its nested waits short.
    if (Configs::dataManager->settingsRepo->prepare_exit || exitController()->exitInProgress()) return;
    exitController()->setExitInProgress(true);
    // A request made during the teardown below may overwrite exit_reason.
    const auto reason = exit_reason;
    const bool unattended = m_exitUnattended;
    const bool restart = reason == ExitReason::Restart || reason == ExitReason::RestartWithTun ||
                         reason == ExitReason::RestartElevated;
    const auto program = QApplication::applicationFilePath();
    QStringList arguments;
    if (restart) {
        arguments = Configs::dataManager->settingsRepo->argv;
        if (arguments.length() > 0) {
            arguments.removeFirst();
            arguments.removeAll("-tray");
            arguments.removeAll("-flag_restart_tun_on");
        }
        if (reason == ExitReason::RestartWithTun) arguments << "-flag_restart_tun_on";
        if (unattended && m_exitKeepTray) arguments << "-tray";
    }

    bool relaunched = false;
#ifdef Q_OS_WIN
    if (reason == ExitReason::RestartWithTun || reason == ExitReason::RestartElevated) {
        // Asked before the teardown so a declined UAC prompt leaves this instance running; the new one waits for this pid to exit.
        const auto elevatedArguments = arguments + QStringList{QString("-wait_pid=%1").arg(QCoreApplication::applicationPid())};
        const bool launched = WinCommander::runProcessElevated(program, elevatedArguments, QApplication::applicationDirPath(), 1, false) == 0;
        if (!launched) {
            exitController()->setExitInProgress(false);
            exit_reason = ExitReason::None;
            MW_show_log(tr("Restart as administrator was cancelled, qThrone keeps running"));
            return;
        }
        relaunched = true;
    }
#endif

    Engine::host()->shutdown(!unattended);
    if (reason == ExitReason::RunUpdater) {
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
        // Until this process exits it holds the data dir lock and still answers on the instance socket.
        arguments << QString("-wait_pid=%1").arg(QCoreApplication::applicationPid());
        QProcess::startDetached(program, arguments);
    }
    QCoreApplication::quit();
}

void MainWindow::request_unattended_exit(ExitReason reason) {
    if (Configs::dataManager->settingsRepo->prepare_exit || exitController()->exitInProgress()) return;
    m_exitUnattended = true;
    m_exitKeepTray = isHidden();
    exit_reason = reason;
    on_menu_exit_triggered();
}

void MainWindow::toggle_system_proxy() {
    auto currentState = Configs::dataManager->settingsRepo->spmode_system_proxy;
    if (currentState) {
        connection()->setSystemProxy(false);
    } else {
        connection()->setSystemProxy(true);
    }
}

void MainWindow::toggle_tun() {
    const auto state = connection()->connectionState();
    if (state == ConnectionState::Connecting || state == ConnectionState::Stopping) return;
    connection()->setTun(!Configs::dataManager->settingsRepo->spmode_vpn);
}

bool MainWindow::StopVPNProcess() {
    supervisor()->killForPrivilegeChange();
    return true;
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
                                tr("Turn off the kill switch? Traffic that does not go through qThrone will no longer be blocked."),
                                QMessageBox::NoButton, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    auto *disable = box->addButton(tr("Disable Kill Switch"), QMessageBox::DestructiveRole);
    auto *cancel = box->addButton(QMessageBox::Cancel);
    // Cancel is the default, so a stray Enter cannot complete the two-step disable.
    box->setDefaultButton(cancel);
    box->setEscapeButton(cancel);
    connect(box, &QMessageBox::finished, this, [this, box, disable] {
        if (box->clickedButton() == disable) connection()->disableKillSwitch();
    });
    box->open();
}

namespace {

bool isNewer(QString assetName) {
    return ReleaseVersion::IsNewer(ReleaseVersion::FromAsset(assetName), QString(NKR_VERSION));
}

constexpr auto dashboardDownloadURL = "https://github.com/SagerNet/sing-box-dashboard/archive/refs/heads/gh-pages.zip";

} // namespace

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
        Engine::Dashboard::SeedApi();
        // Fragment, not query: browsers never send it to the server.
        QUrl url(QString("http://127.0.0.1:%1/dashboard/throne.html").arg(port));
        url.setFragment(QString("secret=%1&url=127.0.0.1:%2")
                            .arg(QString::fromUtf8(QUrl::toPercentEncoding(Configs::dataManager->settingsRepo->core_box_api_secret)))
                            .arg(port),
                        QUrl::StrictMode);
        QDesktopServices::openUrl(url);
    };

    Engine::Dashboard::SeedApi();
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
    QJsonArray array = QString2QJsonArray(resp.data);
    for (const QJsonValue value : array) {
        QJsonObject release = value.toObject();
        if (release["prerelease"].toBool() && !Configs::dataManager->settingsRepo->allow_beta_update) continue;
        for (const QJsonValue asset : release["assets"].toArray()) {
            if (asset["name"].toString().contains(search) && asset["name"].toString().section('.', -1) == QString("zip")) {
                if (!release_download_url.isEmpty() && !ReleaseVersion::IsNewer(
                        ReleaseVersion::FromAsset(asset["name"].toString()), ReleaseVersion::FromAsset(assets_name))) continue;
                note_pre_release = release["prerelease"].toBool() ? " (Pre-release)" : "";
                release_url = release["html_url"].toString();
                release_note = release["body"].toString();
                assets_name = asset["name"].toString();
                release_download_url = asset["browser_download_url"].toString();
                break;
            }
        }
    }

    if (release_download_url.isEmpty() || !isNewer(assets_name)) {
        runOnUiThread([=,this] {
            MessageBoxInfo(QObject::tr("Update"), QObject::tr("No update"));
        });
        return;
    }

    runOnUiThread([=,this] {
        const auto headless = Sys::HostPresence::OtherHeadless();
        const bool headlessSameInstall = headless && Sys::HostPresence::SameInstall(*headless, QCoreApplication::applicationDirPath());
        auto allow_updater = !Configs::dataManager->settingsRepo->flag_use_appdata && !headlessSameInstall;
#ifdef Q_OS_MACOS
        allow_updater = false;
#endif
        QMessageBox box(QMessageBox::Question, QObject::tr("Update") + note_pre_release,
                        QObject::tr("Update found: %1\nRelease note:\n%2").arg(assets_name, release_note));
        if (headlessSameInstall) {
            box.setInformativeText(tr("Update is unavailable while a background qThrone (PID %1) runs from this installation; stop it first.")
                                       .arg(headless->pid));
        }
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
