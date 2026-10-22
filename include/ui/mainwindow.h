#pragma once

#include <QMainWindow>
#include "include/ui/widget/SeasonalDecoration.hpp"
#include <include/global/HTTPRequestHelper.hpp>
#ifndef Q_MOC_RUN
#include <core/gen/libcore.pb.h>
#endif

#include "include/global/Configs.hpp"
#include "include/stats/connections/connectionLister.hpp"
#include "3rdparty/qv2ray/v2/ui/widgets/speedchart/SpeedWidget.hpp"
#include "include/database/entities/Profile.h"
#ifdef Q_OS_LINUX
#include <QtDBus>
#endif

#ifndef MW_INTERFACE

#include <deque>
#include <optional>
#include <QKeyEvent>
#include "include/ui/widget/TrayIcon.hpp"
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QQueue>
#include <QWaitCondition>
#include <QProcess>
#include <QTextDocument>
#include <QShortcut>
#include <QKeySequence>
#include <QSet>
#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QToolButton>
#include <QCheckBox>
#include <QSemaphore>
#include <QMutex>
#include <QThreadPool>
#include <QLocalServer>
#include <QLocalSocket>

#include "group/GroupSort.hpp"
#include "include/global/GuiUtils.hpp"
#include "include/ui/setting/Icon.hpp"
#include "include/ui/utils/DataViewHtmlGenerator.h"
#include "include/ui/utils/ProfilesFilterProxyModel.h"
#include "include/ui/utils/ProfilesTableModel.h"
#include "ui_mainwindow.h"

#endif

namespace Configs_sys {
    class CoreProcess;
}

namespace Configs {
    enum simpleAction : int;
}

namespace RemoteApi {
    class Router;
}

class QMessageBox;
class TrayProfileSelector;
class TrayOtpCodes;
class GlobalHotkeys;
class TestRunner;
class DialogVpnAuth;
class DialogScanner;
class DialogIpLists;
struct VpnAuthChallenge;

struct VpnEndpointState {
    QString tag;
    QString state;
    QString error;
    bool connected = false;
    bool authFailed = false;
};

namespace Qv2ray::ui { class SyntaxHighlighter; }

QT_BEGIN_NAMESPACE
namespace Ui {
    class MainWindow;
}
QT_END_NAMESPACE

enum class RefreshAnchor {
    KeepPlace,
    Removal,
};

enum class ExitReason {
    None,
    RunUpdater,
    Restart,
    RestartWithTun,
    RestartElevated,
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    enum class ConnectionState { Idle, Connecting, Running, Stopping };

    enum class StartOutcome {
        Started,
        Exiting,
        NotFound,
        GroupUnavailable,
        KillSwitchInactive,
        Superseded,
        CoreUnavailable,
        BuildFailed,
        Busy,
        ExtraCoreBlocked,
        GeoAssetsMissing,
        StrictRouteUnavailable,
        TunFailed,
        StartFailed,
    };

    struct StartRequest {
        int profileId = -1;
        // Unattended callers get every failure through start_finished and the log, never a dialog.
        bool interactive = true;
        quint64 serial = 0;
    };

    struct ModeChange {
        bool save = true;
        // Unattended callers get no dialog or elevation prompt, and their restart is an unattended StartRequest carrying restartSerial.
        bool interactive = true;
        bool restart = true;
        quint64 restartSerial = 0;
    };

    explicit MainWindow(QWidget *parent = nullptr);

    ~MainWindow() override;

    qint64 GetCorePid();
    QString GetRunningConfigName();

    // The two live VPN queries below block on an RPC; never call them from the UI thread.
    static QString liveVpnConnectOkText();

    static QString liveVpnStateText(bool *connected = nullptr);

    void prepare_exit();

    void refresh_proxy_list(const QList<int> &ids = {}, bool mayNeedReset = false,
                            RefreshAnchor anchor = RefreshAnchor::KeepPlace);

    void show_group(int gid);

    void show_group_tab_menu(const QPoint &tabBarPos);

    void refresh_groups();

    void updateTabToolTip(int gid);

    void refresh_status(const QString &traffic_update = "");

    void update_traffic_graph(int proxyDl, int proxyUp, int directDl, int directUp);

    void profile_start(int _id = -1);

    void profile_start(const StartRequest &request);

    void profile_stop(bool crash = false, bool block = false, bool manual = false, bool interactive = true);

    int get_profile_to_start();

    // Started, then last running, then remembered profile; unlike get_profile_to_start(), ignores the table selection.
    int resolve_last_profile();

    ConnectionState connection_state() const;

    quint64 next_start_serial();

    void set_spmode_system_proxy(bool enable, bool save = true);

    void toggle_system_proxy();

    void set_spmode_vpn(bool enable, bool save = true);

    // Each returns whether it restarted the profile.
    bool set_spmode_system_proxy(bool enable, const ModeChange &change);

    bool set_spmode_vpn(bool enable, const ModeChange &change);

    bool get_elevated_permissions(bool interactive = true);

    void start_select_mode(QObject *context, const std::function<void(int)> &callback);

    // Returns a line per global hotkey that could not be registered.
    QStringList RegisterHotkey(bool unregister);

    bool IsGlobalHotkeySupported() const;

    bool StopVPNProcess();

    void RestartCore();

    // Whole poll snapshot in the lister's order, never a delta. UI thread only.
    void UpdateConnectionList(const QList<Stats::ConnectionMetadata>& connections);

    void UpdateDataView(bool force = false);

    void noteRestartNeeded(const QString& reason);

    void clearRestartNeeded();

    void refresh_auto_selector_view();

    class DialogAutoSelector *m_autoSelectorDialog = nullptr;

    void setDownloadReport(const DownloadProgressReport& report, bool show);
    void showNotice(const QString &message, int durationMs = 5000);

    void showIpListsDialog(int selectListId = -1);

    void showScannerDialog();

    void refreshScannerDataView(bool force = false);

signals:

    void profile_selected(int id);

    void connection_state_changed(MainWindow::ConnectionState state);

    void start_finished(quint64 serial, int profileId, MainWindow::StartOutcome outcome, const QString &error);

    void stop_finished(int profileId);

public slots:

    void on_commitDataRequest();

    void on_menu_exit_triggered();

#ifndef MW_INTERFACE

private slots:

    void on_masterLogBrowser_customContextMenuRequested(const QPoint &pos);

    void on_menu_basic_settings_triggered();

    void on_menu_routing_settings_triggered();

    void on_menu_vpn_settings_triggered();
    void on_menu_vk_relay_settings_triggered();

    void on_menu_preset_settings_triggered();

    void on_menu_otp_manager_triggered();

    void on_menu_scanner_triggered();

    void on_menu_integration_settings_triggered();

    void on_menu_add_from_input_triggered();

    void on_menu_add_from_clipboard_triggered();

    void on_menu_clone_triggered();

    void on_menu_delete_repeat_triggered();

    void on_menu_delete_triggered();

    void on_menu_reset_traffic_triggered();

    void on_menu_copy_links_triggered();

    void on_menu_copy_links_nkr_triggered();

    void on_menu_export_config_triggered();

    void display_qr_link(bool nkrFormat = false);

    void on_menu_scan_qr_triggered();

    void on_menu_clear_test_result_triggered();

    void on_menu_manage_groups_triggered();

    void on_menu_select_all_triggered();

    void on_menu_remove_unavailable_triggered();

    void on_menu_remove_invalid_triggered();

    void on_menu_remove_insecure_triggered();

    void on_menu_resolve_selected_triggered();

    void on_menu_resolve_domain_triggered();

    void on_menu_update_subscription_triggered();

    void on_profilesTableView_doubleClicked(const QModelIndex &index);

    void on_profilesTableView_customContextMenuRequested(const QPoint &pos);

    void on_tabWidget_currentChanged(int index);

    void on_tabWidget_customContextMenuRequested(const QPoint& p);

private:
    Ui::MainWindow *ui;
    SeasonalDecoration *seasonalDecoration = nullptr;
    QElapsedTimer sinceWindowDeactivated;
    ProfilesTableModel *profilesTableModel = nullptr;

    ProfilesFilterProxyModel *profilesFilterModel = nullptr;
    TrayIcon *tray;
    QMenu *trayMenu = nullptr;
    QAction *trayConnectAction = nullptr;
    QPointer<TrayProfileSelector> traySelector;
    void openTraySelector(bool routing);
    QPointer<TrayOtpCodes> trayOtpCodes;
    void openTrayOtpCodes();
    QThreadPool *parallelCoreCallPool = new QThreadPool(this);
    std::unique_ptr<TestRunner> testRunner;
    Configs_sys::CoreProcess *core_process = nullptr;
    QMutex coreProcessMutex;
    QLocalServer *core_server = nullptr;
    bool rpc_started = false;
    qint64 vpn_pid = 0;
    QTextDocument *qvLogDocument = new QTextDocument(this);
    QString title_error;
    std::optional<Icon::TrayIconStatus> icon_status;
    std::shared_ptr<Configs::Profile> running;
    int last_running_profile_id = -1;
    bool m_profileConnecting = false;
    bool m_profileDisconnecting = false;
    ConnectionState m_lastConnectionState = ConnectionState::Idle;
    quint64 m_startSerial = 0;
    bool m_xrayGeoAssetBusy = false;
    bool m_ruleSetUpdateBusy = false;
    QString traffic_update_cache;
    qint64 last_test_time = 0;
    int proxy_last_order = -1;
    bool select_mode = false;
    QMutex mu_starting;
    QMutex mu_stopping;
    QMutex mu_exit;
    ExitReason exit_reason = ExitReason::None;
    QMutex mu_download_update;
    QMutex mu_download_dashboard;
    class ConnectionsTreeModel *connectionsModel = nullptr;
    class ConnectionsTreeFilterProxyModel *connectionsFilterModel = nullptr;
    class ConnectionsFilterHeader *connectionFilterHeader = nullptr;
    QHash<QString, bool> m_processExpanded; // per-process choices; the rest follow m_processesExpandedByDefault
    bool m_processesExpandedByDefault = false;
    QTimer *connectionFilterDebounce = nullptr;
    QToolButton *connectionExpandButton = nullptr;
    QToolButton *connectionCloseAllButton = nullptr;
    QIcon connectionCloseIcon;
    QIcon connectionExpandIcon;
    QIcon connectionCollapseIcon;
    int toolTipID;
    SpeedWidget *speedChartWidget;
    struct LiveRates {
        qint64 proxyUp = 0;
        qint64 proxyDown = 0;
        qint64 directUp = 0;
        qint64 directDown = 0;
    };
    LiveRates m_liveRates;
    QElapsedTimer m_liveRatesAt;
    class RuntimeStatsWidget *runtimeStatsWidget = nullptr;
    std::atomic<qint64> lastUpdatedMs = QDateTime::currentMSecsSinceEpoch();
    DataViewHtmlGenerator dataViewHtmlGenerator_;

    QList<QShortcut*> hiddenMenuShortcuts;
    GlobalHotkeys *globalHotkeys = nullptr;

    QString addressFilterString;
    QString nameFilterString;
    QString typeFilterString;
    QString countryFilterString;

    QTimer *m_filterRefreshDebounce = nullptr;

    bool m_profilesTableHadFocus = false;
    int m_profilesScrollValue = 0;

    QStringList includeKeywords;
    QStringList excludeKeywords;
    QRegularExpression includeCombined;
    QRegularExpression excludeCombined;
    QMutex logMutex;
    QQueue<QString> logQueue;
    QWaitCondition logWaiter;
    Qv2ray::ui::SyntaxHighlighter *logHighlighter = nullptr;

    QMutex logPendingMutex;
    QString logPendingText;
    bool logFlushScheduled = false;

    // UI-thread view state. m_logLines is what the view renders (capped at max_log_line);
    // while the user is scrolled up, arrivals wait in m_logHeld so the view stays frozen.
    struct LogLine {
        QString text;
        bool visible = true;
    };
    std::deque<LogLine> m_logLines;
    std::deque<QString> m_logHeld;
    bool m_logFollow = true;
    QRegularExpression m_logSearch;
    QTimer *m_logSearchDebounce = nullptr;
    QToolButton *logFilterButton = nullptr;
    QToolButton *logJumpLatestButton = nullptr;

    struct LogFilter {
        bool enableInclude = false;
        bool enableExclude = false;
        QStringList includeKeywords;
        QStringList excludeKeywords;
        QRegularExpression includeCombined;
        QRegularExpression excludeCombined;
    };

    void append_log(const QString &log);

    void log_process_loop();

    // UI thread only.
    void flush_log_batch();

    void setupLogView();

    void releaseHeldLogs();

    // Caps both queues at max_log_line; returns how many dropped lines were visible (rendered blocks).
    int trimLogLines();

    void rebuildLogView();

    void applyLogSearch();

    void setLogFilterVisible(bool visible);

    void updateLogStatus();

    bool should_print_log(const QString &log, const LogFilter &filter);

    void updateLogFilterFields();

    void setLogHighlighter(bool darkMode);

    void applyProfileFilters();

    QList<int> get_now_selected_list();
    void refresh_startstop_button();

    QList<int> get_selected_or_group();

    // Queued on one worker thread in call order; wait blocks until this change has run.
    void set_system_proxy(bool enable, bool wait = false);

    void saveProfileFocusState();

    void restoreProfileFocusState(RefreshAnchor anchor);

    void selectProfileRows(const QList<int> &rows);

    void focusProfilesTable(bool selectFirst);

    void clearUnavailableProfiles(bool confirm = true, QList<int> profileIDs = {});

    // Returns how many profiles it cleared.
    int clear_test_results(const QList<int> &profileIds);

    // Returns whether it restarted the profile.
    bool choose_route(int routeId, bool interactive = true, quint64 restartSerial = 0);

    void dialog_message_impl(MwMessage cmd, const QStringList &args);

    void handle_deeplink_impl(const QString &url);

    void handle_addsub(const QString &url, const QString &name);

    void handle_import_route(const QString &url);

    void handle_add_remote_routes(const QString &url);

    void import_or_handle_deeplink(const QString &text);

    void import_text(const QString &text);

    void refresh_proxy_list_column_size();

    void refresh_proxy_list_impl(const QList<int> &ids = {}, bool mayNeedReset = false);

    void refresh_proxy_list_impl_refresh_data(const QList<int>& ids = {}, bool mayNeedReset = false);

    void parseQrImage(const QPixmap *image);

    void importFromFiles(const QStringList &paths);

    void trayClickEvent();

    void keyPressEvent(QKeyEvent *event) override;

    void closeEvent(QCloseEvent *event) override;

    void changeEvent(QEvent *event) override;

    void showEvent(QShowEvent *event) override;

    void hideEvent(QHideEvent *event) override;

    void syncConnectionViewState();

    void dragEnterEvent(QDragEnterEvent *event);

    void dropEvent(QDropEvent* event) override;

    void applyLogBrowserFont();

    void applyTopBarMetrics();
    bool usesTightLabels() const;

    QSize designMinimumSize;

    QTimer *m_proxyListRefreshDebounce = nullptr;
    void scheduleProxyListRefresh();

    bool m_adjustingColumns = false;

    void HotkeyEvent(const QString &id);

    void toggle_connection();

    void toggle_tun();

    void fail_start(const StartRequest &request, StartOutcome outcome, const QString &title, const QString &error);

    void defer_start_to_core(const StartRequest &request);

    void RegisterHiddenMenuShortcuts(bool unregister = false);
    void registerMenuShortcuts(QMenu *menu, QSet<QKeySequence> &claimed);
    void collectMenuShortcuts(QMenu *menu, QSet<QKeySequence> &out);

    void setActionsData();

    QList<QAction*> getActionsForShortcut();

    void loadShortcuts();

    void setup_rpc(QLocalSocket *socket);

    bool verify_core_pid(QLocalSocket *socket);

    void rank_auto_selector(const std::shared_ptr<Configs::Profile>& ent, const QList<int>& stale = {});

    void on_auto_selector_exhausted(int profileID);

    void on_subscription_group_changed(int gid, const QList<int>& disturbed);

    bool auto_selector_ranked = false;

    bool handleXrayGeoAssetError(const QString& error, const QString& contextName, bool prompt = true);

    void url_test_current();

    static std::shared_ptr<Configs::Profile> vpn_exit_endpoint(const std::shared_ptr<Configs::Profile> &ent);

    static QString vpn_state_text(const QString &state, const QString &error);

    void start_vpn_challenge_poll();

    void stop_vpn_challenge_poll();

    void poll_vpn_challenges();

    void show_vpn_challenge(const VpnAuthChallenge &challenge);

    // True once the challenge is either submitted or deliberately held back for a fresher code.
    bool auto_answer_vpn_challenge(const VpnAuthChallenge &challenge);

    void submit_vpn_challenge_answer(const VpnAuthChallenge &challenge, const QString &username,
                                     const QString &password, const QString &secret,
                                     const QMap<QString, QString> &formValues);

    void show_vpn_auth_failure(const QString &endpointTag, const QString &error);

    bool auto_restart_for_vpn_auth(const QString &endpointTag, int profileID);

    void update_vpn_endpoint_states(const QList<VpnEndpointState> &states);

    void reset_vpn_endpoint_tracking();

    void clear_vpn_credential_overrides();

    void kill_switch_state_changed();

    void show_kill_switch_problem();

    void show_startstop_menu();

    void confirm_disable_kill_switch();

    void disable_kill_switch();

    // Linux/macOS: a core started before the kill switch was on has not adopted the guard group.
    bool core_lacks_guard_identity();

    bool guard_core_restart_pending() const;

    // The restarted core starts the request through CoreStarted; a start requested meanwhile replaces it.
    void restart_core_for_guard(const StartRequest &request);

    QPointer<QMessageBox> m_killSwitchDialog;
    bool m_killSwitchWasFailed = false;
    bool m_killSwitchWasArmed = false;
    StartRequest m_killSwitchDeferredStart;
    // CoreStarted carries only a profile id, so the rest of the request waits here.
    StartRequest m_coreStartRequest;
    QElapsedTimer m_guardCoreRestart;

    QTimer *m_vpnChallengeTimer = nullptr;
    std::atomic<bool> m_vpnChallengeBusy{false};
    QSet<QString> m_vpnChallengeSeen;
    QPointer<DialogVpnAuth> m_vpnAuthDialog;
    QPointer<DialogScanner> m_scannerDialog;
    QPointer<DialogIpLists> m_ipListsDialog;
    QHash<int, QString> m_scannerNames;
    bool m_scannerPanelShown = false;
    QString m_vpnEndpointState;
    QString m_vpnTroubleSummary;
    QString m_vpnTroubleDetail;
    QHash<QString, QString> m_vpnEndpointLastState;
    QHash<QString, QString> m_vpnOtpLastCode;
    QHash<QString, int> m_vpnOtpRejects;
    QSet<QString> m_vpnChallengeAnswering;
    QHash<int, int> m_vpnAutoRestarts;
    qint64 m_vpnAutoRestartAt = 0;
    // Survives the restart the recovery itself triggers, so a rejected retry cannot loop.
    QHash<int, int> m_vpnAuthPrompted;
    int m_vpnAuthRestartID = -1;

    void CheckUpdate();

    void OpenDashboard();

    void SeedDashboard();

    void setupConnectionList();

    void setupConnectionSortMenu();

    void onConnectionContextMenu(const QPoint &pos);

    QString routeRuleAppendBlocker() const;

    enum class RuleToggle { Failed, Added, Moved, Removed };

    // Adds rawRule to the action's simple rules of the current profile, or takes it out when it is already there.
    RuleToggle toggleRuleInCurrentRoute(const QString &rawRule, Configs::simpleAction action);

    void setupConnectionFilter();

    void restoreConnectionSort();

    void applyConnectionSort(Stats::ConnectionSort sort);

    void applyConnectionFilters();

    void syncConnectionSourceColumn();

    void syncConnectionExpansion();

    void setConnectionGroupsExpanded(bool expanded);

    bool connectionGroupsExpanded() const;

    void syncConnectionExpandButton();

    void closeConnections(const QStringList &ids);

    QStringList listedConnectionIds() const;

    void refreshConnectionIcons();

    friend class TestRunner;
    friend class RemoteApi::Router;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

#endif // MW_INTERFACE
};

inline MainWindow *GetMainWindow() {
    return qobject_cast<MainWindow *>(mainwindow);
}

void UI_InitMainWindow();

#ifdef Q_OS_LINUX
class OrgFreedesktopPortalRequestInterface : public QDBusAbstractInterface
{
    Q_OBJECT
public:
    OrgFreedesktopPortalRequestInterface(const QString& service,
                                         const QString& path,
                                         const QDBusConnection& connection,
                                         QObject* parent = nullptr);

    ~OrgFreedesktopPortalRequestInterface();

public Q_SLOTS:
    inline QDBusPendingReply<> Close()
    {
        QList<QVariant> argumentList;
        return asyncCallWithArgumentList(QStringLiteral("Close"), argumentList);
    }

Q_SIGNALS:
    void Response(uint response, QVariantMap results);
};

namespace org {
namespace freedesktop {
namespace portal {
typedef ::OrgFreedesktopPortalRequestInterface Request;
}
}
}
#endif
