#include "include/ui/mainwindow.h"
#include "NkrVersion.h"
#include "include/ui/widget/SeasonalDecoration.hpp"

#include <QApplication>
#include <QHeaderView>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>

#include "include/api/RPC.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/global/LocalNetwork.hpp"
#include "include/stats/autoselector/AutoSelectorMonitor.hpp"
#include "include/sys/KillSwitch.hpp"
#include "include/ui/setting/Icon.hpp"
#include "include/ui/stats/dialog_auto_selector.h"
#include "include/ui/utils/ProfilesTableFilterHeader.h"
#include "include/ui/utils/ProfilesTableModel.h"
#include "include/ui/widget/StartStopButton.hpp"

// Mirrors the language switch in main.cpp; QLocale() alone follows the system, not an explicit setting.
bool MainWindow::usesTightLabels() const {
    static const QStringList byLanguageSetting = {"", "en", "zh_CN", "fa_IR", "ru_RU"};
    const int language = Configs::dataManager->settingsRepo->language;
    const QString locale = language == 0 ? QLocale().name() : byLanguageSetting.value(language);
    return locale.startsWith("zh") || locale.startsWith("ru");
}

void MainWindow::applyTopBarMetrics() {
    const QList<QToolButton*> menuButtons = {
        ui->toolButton_program, ui->toolButton_preferences, ui->toolButton_testing,
        ui->toolButton_routing, ui->toolButton_tools,
    };
    // Drop the previous run's floor: a stale minimum gets baked into minimumSizeHint() below.
    for (auto* b : menuButtons) b->setMinimumWidth(0);

    int uniformButtonWidth = 0;
    for (auto* b : menuButtons) {
        b->ensurePolished();
        uniformButtonWidth = qMax(uniformButtonWidth, b->sizeHint().width());
    }

    // CJK glyphs and long RU labels fill QToolButton's one-space slack and run into the chevron (#1665, #1829).
    if (usesTightLabels()) {
        uniformButtonWidth += 2 * fontMetrics().horizontalAdvance(' ');
    }
    for (auto* b : menuButtons) b->setMinimumWidth(uniformButtonWidth);

    const QSize contentMin = minimumSizeHint();
    setMinimumSize(qMax(designMinimumSize.width(), contentMin.width()),
                   qMax(designMinimumSize.height(), contentMin.height()));
}

void MainWindow::UpdateDataView(bool force)
{
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (!force && now - lastUpdatedMs.load() < 100)
    {
        return;
    }
    auto html = dataViewHtmlGenerator_.buildHtml();
    runOnUiThread([=, this] {
        if (seasonalDecoration) seasonalDecoration->setNoticePriority(dataViewHtmlGenerator_.hasPriorityNotice());
        ui->data_view->setHtml(html);
    }, true);
    lastUpdatedMs.store(QDateTime::currentMSecsSinceEpoch());
}

void MainWindow::noteRestartNeeded(const QString& reason)
{
    if (Configs::dataManager->settingsRepo->started_id < 0) return;
    dataViewHtmlGenerator_.addPendingRestartReason(reason);
    UpdateDataView(true);
}

void MainWindow::clearRestartNeeded()
{
    if (!dataViewHtmlGenerator_.hasPendingRestart()) return;
    dataViewHtmlGenerator_.clearPendingRestart();
    UpdateDataView(true);
}

void MainWindow::setDownloadReport(const DownloadProgressReport& report, bool show)
{
    dataViewHtmlGenerator_.setDownloadReport(report, show);
}

void MainWindow::refresh_auto_selector_view()
{
    const auto view = Stats::autoSelectorMonitor->Snapshot();
    dataViewHtmlGenerator_.setAutoSelectorStatus(view.valid ? view.summary() : QString(),
                                                 view.valid ? view.detail() : QString());
    ui->actionAuto_Selector->setVisible(view.valid);
    UpdateDataView();
    if (m_autoSelectorDialog != nullptr) m_autoSelectorDialog->refresh();
}

void MainWindow::updateLogFilterFields() {
    QMutexLocker locker(&logMutex);
    includeKeywords.clear();
    excludeKeywords.clear();
    for (const auto& inKeyword : Configs::dataManager->settingsRepo->log_include_keyword) includeKeywords.append(inKeyword);
    for (const auto& exKeyword : Configs::dataManager->settingsRepo->log_exclude_keyword) excludeKeywords.append(exKeyword);
    includeCombined.setPattern(Configs::dataManager->settingsRepo->log_include_regex.join("|"));
    excludeCombined.setPattern(Configs::dataManager->settingsRepo->log_exclude_regex.join("|"));
    includeCombined.optimize();
    excludeCombined.optimize();
}

void MainWindow::applyProfileFilters()
{
    if (!profilesFilterModel) return;
    profilesFilterModel->setFilters(typeFilterString, addressFilterString, nameFilterString, countryFilterString);
    refresh_proxy_list_column_size();
}

void MainWindow::refresh_status(const QString &traffic_update) {
    const auto* settings = Configs::dataManager->settingsRepo.get();

    auto refresh_speed_label = [=,this] {
        if (settings->disable_traffic_stats) {
            ui->label_speed->setText("");
        }
        else if (traffic_update_cache == "") {
            ui->label_speed->setText(QObject::tr("Proxy: %1\nDirect: %2").arg("", ""));
        } else {
            ui->label_speed->setText(traffic_update_cache);
        }
    };

    if (!traffic_update.isEmpty() && !settings->disable_traffic_stats) {
        traffic_update_cache = traffic_update;
        if (traffic_update == "STOP") {
            traffic_update_cache = "";
        } else {
            refresh_speed_label();
            return;
        }
    }

    refresh_speed_label();

    QString group_name;
    if (running != nullptr) {
        auto group = Configs::dataManager->groupsRepo->GetGroup(running->gid);
        if (group != nullptr) group_name = group->name;
    }

    const QString runningDetail = m_vpnEndpointState.isEmpty()
                                      ? (running ? running->runningCountryInfo : QString())
                                      : m_vpnEndpointState;

    if (QDateTime::currentSecsSinceEpoch() - last_test_time > 2) {
        QString runningLabelText;
        if (running) {
            runningLabelText = QString("[%1] %2").arg(group_name, running->outbound->DisplayName());
            if (!runningDetail.isEmpty()) {
                runningLabelText += "\n" + runningDetail;
            }
        } else {
            runningLabelText = tr("Not Running");
        }
        ui->label_running->setText(runningLabelText);
    }
    const auto inbound_disabled = settings->disable_mixed_inbound;
    auto display_socks = DisplayAddress(settings->inbound_address, settings->inbound_socks_port);
    QString inbound_tip;
    if (!inbound_disabled && LocalNetwork::LanInboundIsWildcard()) {
        if (const auto lan = LocalNetwork::LanAddress(); !lan.isEmpty()) {
            inbound_tip = tr("Listening on all interfaces (%1)").arg(display_socks);
            display_socks = DisplayAddress(lan, settings->inbound_socks_port);
        }
    }
    ui->label_inbound->setText(QString("Mixed: %1").arg(inbound_disabled ? "Disabled" : display_socks));
    ui->label_inbound->setToolTip(inbound_tip);
    syncConnectionSourceColumn();
    ui->checkBox_VPN->setChecked(settings->spmode_vpn);
    ui->checkBox_SystemProxy->setChecked(settings->spmode_system_proxy);
    if (select_mode) {
        ui->label_running->setText(tr("Select") + " *");
        ui->label_running->setToolTip(tr("Select mode, double-click or press Enter to select a profile, press ESC to exit."));
    } else {
        ui->label_running->setToolTip({});
    }

    const auto route = Configs::dataManager->routesRepo->GetRouteProfile(settings->current_route_id);
    const QString activeRouteName = (route && route->name != "Default") ? route->name : "";
    const auto killSwitchState = Sys::KillSwitch::instance()->state();

    auto make_title = [=,this](bool isTray) {
        QStringList tt;
        if (!isTray && Configs::IsAdmin()) tt << "[Admin]";
        if (select_mode) tt << "[" + tr("Select") + "]";
        if (!title_error.isEmpty()) tt << "[" + title_error + "]";
        if (settings->spmode_vpn && !settings->spmode_system_proxy) tt << "[Tun]";
        if (!settings->spmode_vpn && settings->spmode_system_proxy) tt << "[" + tr("System Proxy") + "]";
        if (settings->spmode_vpn && settings->spmode_system_proxy) tt << "[Tun+" + tr("System Proxy") + "]";
        if (settings->kill_switch && killSwitchState == Sys::KillSwitch::State::Armed) tt << "[" + tr("Kill switch") + "]";
        if (settings->kill_switch && killSwitchState == Sys::KillSwitch::State::Failed) tt << "[" + tr("Kill switch: not active") + "]";
        tt << software_name;
        if (!isTray) tt << QString(NKR_VERSION);
        if (!activeRouteName.isEmpty()) {
            tt << "[" + activeRouteName + "]";
        }
        if (running != nullptr) {
            tt << running->outbound->DisplayTypeAndName() + "@" + group_name;
            if (!runningDetail.isEmpty()) {
                tt << runningDetail;
            }
        }
        return tt.join(isTray ? "\n" : " ");
    };

    setWindowTitle(make_title(false));
    if (tray != nullptr) tray->setToolTip(make_title(true));

    refresh_startstop_button();
}

void MainWindow::refresh_startstop_button() {
    // Every change to the start/stop flags or `running` is followed by a call here, on the UI thread.
    if (const auto connState = connection_state(); connState != m_lastConnectionState) {
        m_lastConnectionState = connState;
        emit connection_state_changed(connState);
    }

    auto *btn = ui->toolButton_startstop;
    if (btn == nullptr) return;

    const auto &settings = Configs::dataManager->settingsRepo;

    auto mode = StartStopButton::Mode::Off;
    if (running != nullptr) {
        if (settings->spmode_vpn) mode = StartStopButton::Mode::Tun;
        else if (settings->spmode_system_proxy) mode = StartStopButton::Mode::SystemProxy;
        else mode = StartStopButton::Mode::Core;
    }
    btn->setMode(mode);

    StartStopButton::State state;
    if (m_profileConnecting) state = StartStopButton::State::Connecting;
    else if (m_profileDisconnecting) state = StartStopButton::State::Disconnecting;
    else if (running != nullptr) state = StartStopButton::State::Running;
    else if (get_profile_to_start() >= 0) state = StartStopButton::State::Idle;
    else state = StartStopButton::State::Disabled;
    btn->setState(state);

    auto lock = StartStopButton::Lock::Hidden;
    if (settings->kill_switch) {
        switch (Sys::KillSwitch::instance()->state()) {
            case Sys::KillSwitch::State::Disabled: break;
            case Sys::KillSwitch::State::Arming: lock = StartStopButton::Lock::Pending; break;
            case Sys::KillSwitch::State::Armed:
                lock = running != nullptr && settings->spmode_vpn ? StartStopButton::Lock::Passing : StartStopButton::Lock::Blocking;
                break;
            case Sys::KillSwitch::State::Failed: lock = StartStopButton::Lock::Fault; break;
        }
    }
    btn->setLock(lock);

    if (trayConnectAction != nullptr) {
        if (m_profileConnecting) {
            trayConnectAction->setText(tr("Connecting"));
        } else if (m_profileDisconnecting) {
            trayConnectAction->setText(tr("Disconnecting"));
        } else {
            trayConnectAction->setText(running != nullptr ? tr("Disconnect") : tr("Connect"));
        }
        trayConnectAction->setEnabled(!m_profileConnecting && !m_profileDisconnecting &&
                                      (running != nullptr || get_profile_to_start() >= 0));
    }

    // Here rather than in refresh_status(): the connecting/disconnecting flags only ever refresh this button.
    auto iconStatus = Icon::TrayIconStatus::None;
    if (m_profileConnecting || m_profileDisconnecting) iconStatus = Icon::TrayIconStatus::Connecting;
    else if (mode == StartStopButton::Mode::Tun) iconStatus = Icon::TrayIconStatus::Vpn;
    else if (mode == StartStopButton::Mode::SystemProxy) iconStatus = Icon::TrayIconStatus::SystemProxy;
    else if (mode == StartStopButton::Mode::Core) iconStatus = Icon::TrayIconStatus::Running;
    if (iconStatus != icon_status) {
        QApplication::setWindowIcon(GetTaskbarIcon(iconStatus));
        if (tray != nullptr) tray->setIcon(Icon::GetTrayIcon(iconStatus));
        icon_status = iconStatus;
    }
}

void MainWindow::update_traffic_graph(int proxyDl, int proxyUp, int directDl, int directUp)
{
    m_liveRates = LiveRates{.proxyUp = proxyUp, .proxyDown = proxyDl, .directUp = directUp, .directDown = directDl};
    m_liveRatesAt.start();
    if (speedChartWidget) {
        QMap<SpeedWidget::GraphType, long> pointData;
        pointData[SpeedWidget::OUTBOUND_PROXY_UP] = proxyUp;
        pointData[SpeedWidget::OUTBOUND_PROXY_DOWN] = proxyDl;
        pointData[SpeedWidget::OUTBOUND_DIRECT_UP] = directUp;
        pointData[SpeedWidget::OUTBOUND_DIRECT_DOWN] = directDl;

        speedChartWidget->AddPointData(pointData);
    }
}

void MainWindow::refresh_proxy_list_column_size() {
    const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (!group || !ui->profilesTableView->isVisible()) return;

    auto *hHeader = dynamic_cast<ProfilesTableFilterHeader*>(ui->profilesTableView->horizontalHeader());
    QTimer::singleShot(0, ui->profilesTableView, [=, this]() {
        // The resizeSection / scrollbar-policy changes below re-enter here via valueChanged.
        if (m_adjustingColumns) return;
        m_adjustingColumns = true;
        QScrollBar *vBar = ui->profilesTableView->verticalScrollBar();
        const bool vBarBlocked = vBar->blockSignals(true);
        // Header signals stay live: QTableView repaints its cells from sectionResized.
        constexpr int columnCount = ProfilesTableModel::ColumnCount;
        if (!group->column_width.isEmpty() && group->column_width.size() != columnCount) {
            group->column_width.clear();
        }
        if (group->column_width.isEmpty()) {
            hHeader->setSectionResizeMode(ProfilesTableModel::ColType, QHeaderView::ResizeToContents);
            hHeader->setSectionResizeMode(ProfilesTableModel::ColAddress, QHeaderView::Stretch);
            hHeader->setSectionResizeMode(ProfilesTableModel::ColName, QHeaderView::Stretch);
            hHeader->setSectionResizeMode(ProfilesTableModel::ColTestResult, QHeaderView::ResizeToContents);
            hHeader->setSectionResizeMode(ProfilesTableModel::ColTraffic, QHeaderView::ResizeToContents);
            // ResizeToContents only measures on-screen rows, so pin these or they jitter while scrolling.
            for (int col : {ProfilesTableModel::ColType,
                            ProfilesTableModel::ColTestResult, ProfilesTableModel::ColTraffic}) {
                if (group->calculated_column_width.size() > col &&
                    group->calculated_column_width[col] > hHeader->sectionSize(col)) {
                    hHeader->setSectionResizeMode(col, QHeaderView::Fixed);
                    hHeader->resizeSection(col, group->calculated_column_width[col]);
                }
            }
            ui->profilesTableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            group->clearCalculatedColumnWidth();
            for (int i = 0; i < columnCount; i++) {
                auto size = hHeader->sectionSize(i);
                hHeader->setSectionResizeMode(i, QHeaderView::Interactive);
                hHeader->resizeSection(i, size);
                group->calculated_column_width << size;
            }
        } else {
            group->clearCalculatedColumnWidth();
            for (int i = 0; i < columnCount; i++) {
                hHeader->setSectionResizeMode(i, QHeaderView::Interactive);
                hHeader->resizeSection(i, group->column_width.at(i));
            }
            ui->profilesTableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
        // Stored sizes are proportions, not a fixed canvas wider than the
        // viewport. Fill the table on growth and keep every column on shrink.
        hHeader->fitToWidth(ui->profilesTableView->viewport()->width());
        vBar->blockSignals(vBarBlocked);
        m_adjustingColumns = false;
    });
}

void MainWindow::refresh_proxy_list(const QList<int>& ids, bool mayNeedReset, RefreshAnchor anchor) {
    if (!Configs::dataManager->settingsRepo->refreshing_group) saveProfileFocusState();
    refresh_proxy_list_impl(ids, mayNeedReset);
    if (mayNeedReset) restoreProfileFocusState(anchor);
}

void MainWindow::refresh_proxy_list_impl(const QList<int>& ids, bool mayNeedReset) {
    const auto currentGroup = Configs::dataManager->groupsRepo->CurrentGroup();
    if (currentGroup == nullptr)
    {
        MW_show_log("Could not find current group!");
        return;
    }
    refresh_proxy_list_impl_refresh_data(ids, mayNeedReset);
    refresh_proxy_list_column_size();
}

void MainWindow::refresh_proxy_list_impl_refresh_data(const QList<int>& ids, bool mayNeedReset) {
    const auto currentGroup = Configs::dataManager->groupsRepo->CurrentGroup();
    if (currentGroup == nullptr) return;
    if (!ids.isEmpty()) {
        for (auto id:ids) profilesTableModel->refreshProfileId(id);
    } else {
        profilesTableModel->refreshTable(currentGroup->profiles, mayNeedReset);
    }
}

std::shared_ptr<Configs::Profile> MainWindow::vpn_exit_endpoint(const std::shared_ptr<Configs::Profile> &ent) {
    auto hop = ent;
    // The "proxy" tag lands on the exit hop, and the stored list runs in to out.
    if (hop != nullptr && hop->type == "chain") {
        const auto *chain = hop->Chain();
        if (chain == nullptr || chain->list.isEmpty()) return nullptr;
        hop = Configs::dataManager->profilesRepo->GetProfile(chain->list.back());
    }
    if (hop == nullptr) return nullptr;
    if (hop->type != "openvpn" && hop->type != "openconnect") return nullptr;
    return hop;
}

QString MainWindow::vpn_state_text(const QString &state, const QString &error) {
    if (state == "connected") return MainWindow::tr("Connect OK");
    if (state == "connecting") return MainWindow::tr("Connecting");
    if (state == "auth-pending") return MainWindow::tr("Waiting for authentication");
    if (state == "error") {
        return error.isEmpty() ? MainWindow::tr("Tunnel error")
                               : MainWindow::tr("Tunnel error") + ": " + error;
    }
    return state;
}

QString MainWindow::liveVpnStateText(bool *connected) {
    if (connected != nullptr) *connected = false;
    const int startedID = Configs::dataManager->settingsRepo->started_id;
    if (startedID < 0) return {};
    if (vpn_exit_endpoint(Configs::dataManager->profilesRepo->GetProfile(startedID)) == nullptr) return {};

    bool ok = false;
    const auto status = API::defaultClient->QueryVPNStatus(&ok, {"proxy"});
    if (!ok || status.results.empty()) return {};
    const auto &res = status.results.front();
    if (connected != nullptr) *connected = res.connected.value();
    return vpn_state_text(QString::fromStdString(res.state.value()),
                          QString::fromStdString(res.error.value()));
}

QString MainWindow::liveVpnConnectOkText() {
    bool connected = false;
    const auto text = liveVpnStateText(&connected);
    return connected ? text : QString();
}

void MainWindow::url_test_current() {
    last_test_time = QDateTime::currentSecsSinceEpoch();
    ui->label_running->setText(tr("Testing"));

    runOnNewThread([=,this] {
        libcore::TestReq req;
        req.test_current = true;
        req.url = Configs::dataManager->settingsRepo->test_latency_url.toStdString();

        bool rpcOK;
        auto result = API::defaultClient->Test(&rpcOK, req);
        if (!rpcOK || result.results.empty()) return;

        auto latency = result.results[0].latency_ms.value();
        last_test_time = QDateTime::currentSecsSinceEpoch();
        // Blocking RPC, so it has to resolve here rather than on the UI thread.
        const auto vpnText = latency <= 0 ? liveVpnStateText() : QString();

        runOnUiThread([=,this] {
            if (!result.results[0].error.value().empty()) {
                MW_show_log(QString("UrlTest error: %1").arg(QString::fromStdString(result.results[0].error.value())));
            }
            if (latency <= 0) {
                ui->label_running->setText(tr("Test Result") + ": " + (vpnText.isEmpty() ? tr("Unavailable") : vpnText));
            } else if (latency > 0) {
                ui->label_running->setText(tr("Test Result") + ": " + QString("%1 ms").arg(latency));
            }
        });
    });
}
