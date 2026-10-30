#include "include/engine/tests/TestRunner.h"

#include "include/api/RPC.h"
#include "include/configs/generate.h"
#include "include/configs/outbounds/qwdtt.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/engine/EnginePolicy.h"
#include "include/engine/EngineTr.h"
#include "include/engine/services/AssetUpdates.h"
#include "include/engine/services/ProfileService.h"
#include "include/engine/tests/TestObserver.h"
#include "include/global/Configs.hpp"
#include "include/global/CountryHelper.hpp"
#include "include/stats/traffic/TrafficStatsManager.hpp"

#include <QCoreApplication>
#include <QSemaphore>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <limits>
#include <utility>

namespace Engine {

namespace {
    constexpr int kTestRunnerBatchSize = 100;
    constexpr int kTestRunnerSpeedPollIntervalMs = 100;
    constexpr int kTestRunnerLatencyPollIntervalMs = 200;

    QList<int> testRunnerWithoutAutoSelectors(const QList<int>& profileIDs) {
        const auto selectors = Configs::dataManager->profilesRepo->GetProfileIdsByType("autoselector");
        if (selectors.isEmpty()) return profileIDs;
        const QSet<int> skip(selectors.begin(), selectors.end());
        QList<int> filtered;
        filtered.reserve(profileIDs.size());
        for (int id : profileIDs) {
            if (!skip.contains(id)) filtered << id;
        }
        return filtered;
    }

    bool testRunnerIsTestAborted(const QString& error) {
        return error.contains("test aborted") || error.contains("context canceled");
    }

    bool testRunnerUsesVkCalls(const QString& nativeConfig) {
        const auto backend = QJsonDocument::fromJson(nativeConfig.toUtf8()).object().value("backend").toString();
        return backend.isEmpty() || backend == "csqtt" || backend == "wdttplus";
    }

    // The core runs a native probe once its turn comes, with budgets of its own; Stop still ends it at once.
    constexpr int kTestRunnerNativeRpcSlackMs = 30 * 60 * 1000;

    bool testRunnerIsVpnProfile(const std::shared_ptr<Configs::Profile>& ent) {
        return ent != nullptr && (ent->type == "openvpn" || ent->type == "openconnect");
    }

    // Mirror the core's URLTestTimeout, TunnelStartupTimeout, TunnelHandshakeTimeout and normalizeConcurrency.
    constexpr int kTestRunnerCoreDefaultTimeoutMs = 5000;
    constexpr int kTestRunnerTunnelStartupMs = 5000;
    constexpr int kTestRunnerTunnelHandshakeMs = 10000;
    constexpr int kTestRunnerCoreMaxConcurrency = 100;
    constexpr int kTestRunnerRpcSlackMs = 30000;

    // Test/IPTest answer only once the whole batch is done, so the deadline covers its worst case.
    int testRunnerBatchRpcTimeoutMs(qsizetype tagCount, int requestsPerTag) {
        const auto& settings = Configs::dataManager->settingsRepo;
        int concurrency = settings->test_concurrent;
        if (concurrency <= 0 || concurrency >= 500) concurrency = kTestRunnerCoreMaxConcurrency;
        const qint64 timeoutMs = settings->url_test_timeout_ms > 0 ? settings->url_test_timeout_ms : kTestRunnerCoreDefaultTimeoutMs;
        const qint64 rounds = (qMax<qsizetype>(tagCount, 1) + concurrency - 1) / concurrency;
        const qint64 total = rounds * (kTestRunnerTunnelHandshakeMs + requestsPerTag * timeoutMs + kTestRunnerTunnelStartupMs) + kTestRunnerRpcSlackMs;
        return static_cast<int>(qMin<qint64>(total, std::numeric_limits<int>::max()));
    }

    int testRunnerResolveEntID(const QMap<QString, int>& tag2entID, const std::string& tag, int fallback) {
        if (tag2entID.isEmpty()) return fallback;
        return tag2entID.value(QString::fromStdString(tag), -1);
    }

    // Target is deduced, not named: access control applies to naming a private type.
    template <typename Req, typename Target>
    void testRunnerFillCommonTestReq(Req& req, const Target& target) {
        for (const auto& tag : target.outboundTags) req.outbound_tags.push_back(tag.toStdString());
        req.config = target.coreConfig.toStdString();
        req.qwdtt_config = target.qwdttConfig.toStdString();
        req.use_default_outbound = target.useDefaultOutbound;
        req.xray_config = target.xrayConfig.toStdString();
        req.need_xray = !target.xrayConfig.isEmpty();
        req.xray_outbound_dns_strategy = target.xrayDnsStrategy.toStdString();
        for (const auto& xc : target.xrayFullConfigs) req.xray_full_configs.push_back(xc.toStdString());
    }

    // Stopping does not join: the poll may itself sit in a 30s RPC and must not stall the batch.
    class TestRunnerResultPoller {
    public:
        TestRunnerResultPoller(std::function<void()> tick, int intervalMs)
            : stop_(std::make_shared<std::atomic<bool>>(false)) {
            runOnNewThread([stop = stop_, tick = std::move(tick), intervalMs] {
                while (!stop->load()) {
                    QThread::msleep(intervalMs);
                    if (stop->load()) break;
                    tick();
                }
            });
        }

        ~TestRunnerResultPoller() { stop_->store(true); }

        TestRunnerResultPoller(const TestRunnerResultPoller&) = delete;
        TestRunnerResultPoller& operator=(const TestRunnerResultPoller&) = delete;

    private:
        std::shared_ptr<std::atomic<bool>> stop_;
    };

    constexpr int kTestRunnerPoolThreads = 10;

    class TestRunnerNullObserver final : public TestObserver {
    public:
        void testPanelSeeded(TestKind, int) override {}
        void testProgressed() override {}
        void testPanelsCleared() override {}
        void testViewChanged(bool) override {}
        void speedTestProgress(const QString&, const libcore::SpeedTestResult&) override {}
        void testedProfilesChanged(const QList<int>&) override {}
    };

    TestObserver* testRunnerNullObserver() {
        static TestRunnerNullObserver observer;
        return &observer;
    }
}

TestRunner::TestRunner(EnginePolicy* policy, TestObserver* observer, RunningSnapshot runningSnapshot)
    : policy_(policy),
      observer_(observer != nullptr ? observer : testRunnerNullObserver()),
      runningSnapshot_(std::move(runningSnapshot)) {
    pool_.setMaxThreadCount(kTestRunnerPoolThreads);
    nativeProbePool_.setMaxThreadCount(3);
    vkProbePool_.setMaxThreadCount(12);
}

int TestRunner::runningProfileId() const {
    const auto running = runningSnapshot_();
    return running ? running->id : -1;
}

bool TestRunner::isRunning() {
    return gate_.running();
}

void TestRunner::stop() {
    // Flag first, count second: a probe that read the old count either sees the flag or is refused by the core.
    gate_.stopRequested.store(true);
    const qint64 epoch = ++stopEpoch_;
    runOnNewThread([epoch] {
        bool ok = false;
        API::defaultClient->StopTests(&ok, epoch);
        if (!ok) MW_show_log(MainWindowTr::tr("Failed to stop tests"));
    });
}

QString TestRunner::contextName(int entID) const {
    if (entID != -1) {
        if (auto e = Configs::dataManager->profilesRepo->GetProfile(entID)) return e->outbound->DisplayTypeAndName();
    }
    return MainWindowTr::tr("a tested profile");
}

void TestRunner::applyUrlResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::URLTestResp& res,
                                const QHash<QString, bool>* vpnConnected) {
    const auto error = QString::fromStdString(res.error.value());
    if (error.isEmpty()) {
        ent->SetLatency(res.latency_ms.value());
    } else if (testRunnerIsTestAborted(error) || gate_.stopRequested.load()) {
        // A probe a stop or a repeated press cut short failed only because of the stop.
        ent->SetLatency(0);
    } else if (error.startsWith("qWDTT test not started:")) {
        ent->SetLatency(error.contains("skipped:") ? 0 : -1);
        MW_show_log(MainWindowTr::tr("[%1] test error: %2").arg(ent->outbound->DisplayTypeAndName(), error));
    } else if (vpnConnected != nullptr && testRunnerIsVpnProfile(ent)
               && vpnConnected->value(QString::fromStdString(res.outbound_tag.value()), false)) {
        ent->SetLatency(Configs::kLatencyConnectOnly);
    } else {
        ent->SetLatency(-1);
        MW_show_log(MainWindowTr::tr("[%1] test error: %2").arg(ent->outbound->DisplayTypeAndName(), error));
    }
    Configs::dataManager->profilesRepo->Save(ent);
    runOnUiThread([this, id = ent->id] { clearTestState({id}); });
}

void TestRunner::clearTestState(const QList<int>& ids) {
    QList<int> done;
    for (int id : ids) if (!restarting_.contains(id)) done << id;
    observer_->testStateChanged(done, 0);
}

void TestRunner::applyIpResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::IPTestRes& res) {
    const auto error = QString::fromStdString(res.error.value());
    if (error.isEmpty()) {
        ent->ip_out = QString::fromStdString(res.ip.value());
        ent->test_country = QString::fromStdString(res.country_code.value());
    } else {
        if (!testRunnerIsTestAborted(error)) {
            MW_show_log(MainWindowTr::tr("[%1] IP test error: %2").arg(ent->outbound->DisplayTypeAndName(), error));
        }
        ent->ip_out.clear();
        ent->test_country.clear();
    }
    Configs::dataManager->profilesRepo->Save(ent);
}

// The core buffers each finished result of a shared test box; show it as soon as it arrives.
void TestRunner::pollUrlStream() {
    bool ok = false;
    const auto resp = API::defaultClient->QueryURLTest(&ok);
    if (!ok) return;
    QList<int> updated;
    for (const auto& res : resp.results) {
        const auto tag = QString::fromStdString(res.outbound_tag.value());
        int entid = -1;
        {
            QMutexLocker lock(&streamMu_);
            // A tag no running probe owns is a leftover of an aborted one.
            if (!streamTags_.contains(tag) || streamed_.contains(tag)) continue;
            entid = streamTags_.value(tag);
            streamed_.insert(tag);
        }
        auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
        if (ent == nullptr) continue;
        applyUrlResult(ent, res);
        observer_->testProgressed();
        updated << entid;
    }
    if (updated.isEmpty()) return;
    observer_->testViewChanged(true);
    runOnUiThread([=, this] { observer_->testedProfilesChanged(updated); });
}

void TestRunner::runUrlProbe(const Target& target) {
    const qint64 epoch = stopEpoch_.load();
    if (gate_.stopRequested.load()) {
        MW_show_log(MainWindowTr::tr("Profile test aborted"));
        return;
    }

    libcore::TestReq req;
    testRunnerFillCommonTestReq(req, target);
    req.test_current = target.testCurrent;
    req.url = Configs::dataManager->settingsRepo->test_latency_url.toStdString();
    req.max_concurrency = Configs::dataManager->settingsRepo->test_concurrent;
    req.test_timeout_ms = Configs::dataManager->settingsRepo->url_test_timeout_ms;
    req.stop_epoch = epoch;

    // The test box dies with the RPC, so the verdict has to be asked for up front.
    for (auto it = target.tag2entID.cbegin(); it != target.tag2entID.cend(); ++it) {
        if (testRunnerIsVpnProfile(Configs::dataManager->profilesRepo->GetProfile(it.value()))) {
            req.vpn_endpoint_tags.push_back(it.key().toStdString());
        }
    }

    bool rpcOK = false;
    QString coreError;
    libcore::TestResp result;
    // Only a shared box streams: a single-profile target reports under the ambiguous "proxy" tag.
    const bool streaming = target.qwdttConfig.isEmpty() && !target.outboundTags.isEmpty();
    if (streaming) {
        QMutexLocker lock(&streamMu_);
        for (auto it = target.tag2entID.cbegin(); it != target.tag2entID.cend(); ++it) streamTags_.insert(it.key(), it.value());
    }
    {
        std::unique_ptr<TestRunnerResultPoller> poller;
        if (streaming) poller = std::make_unique<TestRunnerResultPoller>([this] { pollUrlStream(); }, kTestRunnerLatencyPollIntervalMs);
        result = API::defaultClient->Test(&rpcOK, req, &coreError, testRunnerBatchRpcTimeoutMs(target.outboundTags.size(), 2) + (target.qwdttConfig.isEmpty() ? 0 : kTestRunnerNativeRpcSlackMs));
    }
    // What the stream already showed; a late poll tick finds the tags gone and drops its copy.
    QSet<QString> shown;
    if (streaming) {
        QMutexLocker lock(&streamMu_);
        for (auto it = target.tag2entID.cbegin(); it != target.tag2entID.cend(); ++it) {
            streamTags_.remove(it.key());
            if (streamed_.remove(it.key())) shown.insert(it.key());
        }
    }

    if (!rpcOK || result.results.empty()) {
        if (!rpcOK) AssetUpdates::HandleXrayGeoError(policy_, coreError, contextName(target.entID), interactive_.load());
        const auto error = gate_.stopRequested.load() ? QStringLiteral("test aborted") :
            (coreError.isEmpty() ? QStringLiteral("HTTP test returned no result or exceeded its deadline") : coreError);
        for (auto it = target.tag2entID.cbegin(); it != target.tag2entID.cend(); ++it) {
            if (auto ent = Configs::dataManager->profilesRepo->GetProfile(it.value())) {
                libcore::URLTestResp failed;
                failed.outbound_tag = it.key().toStdString(); failed.latency_ms = 0;
                failed.error = error.toStdString();
                applyUrlResult(ent, failed);
            }
        }
        return;
    }

    QHash<QString, bool> vpnConnected;
    for (const auto& st : result.vpn_status) {
        vpnConnected.insert(QString::fromStdString(st.tag.value()), st.connected.value());
    }

    for (const auto& res : result.results) {
        const int entid = testRunnerResolveEntID(target.tag2entID, res.outbound_tag.value(), target.entID);
        if (entid == -1) {
            MW_show_log(MainWindowTr::tr("Something is very wrong, the subject ent cannot be found!"));
            continue;
        }
        auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
        if (ent == nullptr) {
            MW_show_log(MainWindowTr::tr("Profile manager data is corrupted, try again."));
            continue;
        }
        const auto tag = QString::fromStdString(res.outbound_tag.value());
        if (shown.contains(tag)) {
            // Already counted; only a VPN endpoint's connect-only verdict can still change it.
            if (!res.error.value().empty() && testRunnerIsVpnProfile(ent)) {
                applyUrlResult(ent, res, &vpnConnected);
                runOnUiThread([this, entid] { observer_->testedProfilesChanged({entid}); });
            }
            continue;
        }
        applyUrlResult(ent, res, &vpnConnected);
        {
            observer_->testProgressed();
            observer_->testViewChanged(true);
            runOnUiThread([this, entid] { observer_->testedProfilesChanged({entid}); });
        }
    }
}

void TestRunner::runIpProbe(const Target& target) {
    const qint64 epoch = stopEpoch_.load();
    if (gate_.stopRequested.load()) {
        MW_show_log(MainWindowTr::tr("Profile test aborted"));
        return;
    }

    libcore::IPTestRequest req;
    testRunnerFillCommonTestReq(req, target);
    req.test_current = target.testCurrent;
    req.max_concurrency = Configs::dataManager->settingsRepo->test_concurrent;
    req.test_timeout_ms = Configs::dataManager->settingsRepo->url_test_timeout_ms;
    req.stop_epoch = epoch;

    bool rpcOK = false;
    QString coreError;
    libcore::IPTestResp result;
    {
        result = API::defaultClient->IPTest(&rpcOK, req, &coreError, testRunnerBatchRpcTimeoutMs(target.outboundTags.size(), 1) + (target.qwdttConfig.isEmpty() ? 0 : kTestRunnerNativeRpcSlackMs));
    }

    if (!rpcOK || result.results.empty()) {
        if (!rpcOK) AssetUpdates::HandleXrayGeoError(policy_, coreError, contextName(target.entID), interactive_.load());
        return;
    }

    for (const auto& res : result.results) {
        const int entid = testRunnerResolveEntID(target.tag2entID, res.outbound_tag.value(), target.entID);
        if (entid == -1) {
            MW_show_log(MainWindowTr::tr("Something is very wrong, the subject ent cannot be found!"));
            continue;
        }
        auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
        if (ent == nullptr) {
            MW_show_log(MainWindowTr::tr("Profile manager data is corrupted, try again."));
            continue;
        }
        applyIpResult(ent, res);
        {
            observer_->testProgressed();
            observer_->testViewChanged(true);
            runOnUiThread([this, entid] { observer_->testedProfilesChanged({entid}); });
        }
    }
}

bool TestRunner::runUrlTests(const QList<int>& profileIDs, const std::function<void()>& onFinished, bool interactive) {
    return runLatencyGroup(LatencyKind::Url, profileIDs, onFinished, false, interactive);
}

void TestRunner::queueUrlTests(const QList<int>& profileIDs, const std::function<void()>& onFinished, bool interactive) {
    runOnNewThread([=, this] { runLatencyGroup(LatencyKind::Url, profileIDs, onFinished, true, interactive); });
}

bool TestRunner::runIpTests(const QList<int>& profileIDs, const std::function<void()>& onFinished, bool interactive) {
    return runLatencyGroup(LatencyKind::Ip, profileIDs, onFinished, false, interactive);
}

bool TestRunner::runLatencyGroup(LatencyKind kind, const QList<int>& requestedIDs,
                                 const std::function<void()>& onFinished, bool waitForSession, bool interactive) {
    const bool isUrl = kind == LatencyKind::Url;
    const auto panelKind = isUrl ? TestKind::Latency : TestKind::Ip;
    // Must fire on every exit path — a caller may be blocked on it.
    const auto finish = [onFinished] { if (onFinished) onFinished(); };

    auto profileIDs = testRunnerWithoutAutoSelectors(requestedIDs);
    if (profileIDs.isEmpty()) { finish(); return false; }
    if (isUrl && interactive && !waitForSession) {
        QList<int> restart;
        {
            QMutexLocker lock(&urlMu_);
            if (std::any_of(profileIDs.begin(), profileIDs.end(), [this](int id) { return urlIds_.contains(id); })) {
                restart = profileIDs;
                for (int id : urlIds_) if (!restart.contains(id)) restart << id;
            }
        }
        if (!restart.isEmpty()) {
            // Pressing ping again restarts it: the running sweeps stop and their profiles start over at once.
            runOnUiThread([this, restart] {
                restarting_.unite(QSet<int>(restart.begin(), restart.end()));
                observer_->testRestarting(restart);
            });
            stop();
            queueUrlTests(restart, onFinished);
            return true;
        }
    }
    const auto started = gate_.beginLatency(profileIDs, waitForSession);
    if (started != TestSessionGate::Start::Started) {
        if (started == TestSessionGate::Start::Busy) {
            const auto text = MainWindowTr::tr("The last test did not finish completely, please wait. If it persists, please restart the program.");
            if (interactive && QThread::currentThread() == QCoreApplication::instance()->thread()) policy_->testSessionBusy(text);
            else MW_show_log(text);
        }
        finish();
        return false;
    }

    interactive_.store(interactive);
    if (isUrl) {
        QMutexLocker lock(&urlMu_);
        urlIds_.unite(QSet<int>(profileIDs.begin(), profileIDs.end()));
    }
    if (isUrl) runOnUiThread([this, profileIDs] {
        for (int id : profileIDs) restarting_.remove(id);
        observer_->testStateChanged(profileIDs, 1);
    });

    runOnNewThread([this, profileIDs, panelKind, isUrl, finish]() {
        observer_->testPanelSeeded(panelKind, profileIDs.size());
        observer_->testViewChanged(true);

        // Native transports finish on their own: a slow handshake must not hold the next batch back.
        QSemaphore nativeDone;
        int nativeCount = 0;
        auto runBatch = [this, isUrl, &nativeDone, &nativeCount](const QList<std::shared_ptr<Configs::Profile>>& profileSlice, const QList<int>& ids) {
            auto buildObject = Configs::BuildTestConfig(profileSlice);
            if (!buildObject->error.isEmpty()) {
                MW_show_log(MainWindowTr::tr("Failed to build test config for batch: ") + buildObject->error);
                return;
            }

            QSemaphore batchDone;
            int testCount = 0;
            const auto probe = [this, isUrl, &batchDone, &testCount, &nativeDone, &nativeCount](const Target& target) {
                const bool native = !target.qwdttConfig.isEmpty();
                auto *pool = !native ? &pool_ : testRunnerUsesVkCalls(target.qwdttConfig) ? &vkProbePool_ : &nativeProbePool_;
                auto *done = native ? &nativeDone : &batchDone;
                ++(native ? nativeCount : testCount);
                pool->start([this, isUrl, target, done] {
                    const QSemaphoreReleaser releaser(*done);
                    if (isUrl) runUrlProbe(target);
                    else runIpProbe(target);
                });
            };

            QList<Target> individualTargets;
            for (const auto& entID : buildObject->fullConfigs.keys()) {
                Target target;
                target.coreConfig = buildObject->fullConfigs[entID];
                target.useDefaultOutbound = true;
                target.entID = entID;
                target.qwdttConfig = buildObject->qwdttConfigs.value(entID);
                if (isUrl && !target.qwdttConfig.isEmpty()) {
                    auto config = QJsonDocument::fromJson(target.qwdttConfig.toUtf8()).object();
                    // qWDTT is pinged by one TURN/DTLS handshake with the server's DTLS port, also in RAW mode.
                    const auto profile = Configs::dataManager->profilesRepo->GetProfile(entID);
                    if (profile && profile->type == "qwdtt") {
                        if (const auto native = std::dynamic_pointer_cast<Configs::qwdtt>(profile->outbound)) {
                            config = QJsonDocument::fromJson(native->NativePingConfig(target.qwdttConfig).toUtf8()).object();
                        }
                    }
                    // URL checks keep automatic captcha retries, without blocking the batch on a manual window.
                    config.insert("interactive_captcha", false);
                    config.insert("latency_probe", true);
                    target.qwdttConfig = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
                }
                target.testCurrent = !target.qwdttConfig.isEmpty() && runningProfileId() == entID;
                target.tag2entID.insert("proxy", entID);
                individualTargets.append(target);
            }
            if (!buildObject->outboundTags.empty()) {
                Target target;
                target.coreConfig = QJsonObject2QString(buildObject->coreConfig, false);
                target.xrayConfig = buildObject->isXrayNeeded ? QJsonObject2QString(buildObject->xrayConfig, false) : "";
                target.xrayFullConfigs = buildObject->xrayFullConfigs;
                target.outboundTags = buildObject->outboundTags;
                target.tag2entID = buildObject->tag2entID;
                target.xrayDnsStrategy = buildObject->xrayDnsStrategy;
                probe(target);
            }
            for (const auto &target : individualTargets) probe(target);
            batchDone.acquire(testCount);
            if (testCount == 0) return;

            MW_show_log(isUrl ? "URL test for batch done." : "IP test for batch done.");
            runOnUiThread([=, this] {
                observer_->testedProfilesChanged(ids);
            });
        };

        std::shared_ptr<Configs::Group> currentGroup;
        QHash<int, QList<int>> tested;
        for (int i = 0; i < profileIDs.length(); i += kTestRunnerBatchSize) {
            if (gate_.stopRequested.load()) break;
            const auto profileIDsSlice = profileIDs.mid(i, kTestRunnerBatchSize);
            auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(profileIDsSlice);
            if (isUrl && !currentGroup && !profiles.isEmpty()) {
                currentGroup = Configs::dataManager->groupsRepo->GetGroup(profiles[0]->gid);
            }
            for (const auto& profile : profiles) tested[profile->gid] << profile->id;
            runBatch(profiles, profileIDsSlice);
        }
        nativeDone.acquire(nativeCount);
        // Once the run is over, so rows do not jump while results stream in.
        Configs::RepositionLater(tested, Configs::SortInput::TestResult);

        if (isUrl) {
            QMutexLocker lock(&urlMu_);
            for (int id : profileIDs) urlIds_.remove(id);
        }
        runOnUiThread([this, profileIDs] { clearTestState(profileIDs); }, true);
        if (gate_.finishLatency(profileIDs)) observer_->testPanelsCleared();
        observer_->testViewChanged(true);
        finish();

        if (currentGroup != nullptr && currentGroup->auto_clear_unavailable) {
            MW_show_log("URL test finished, clearing unavailable profiles...");
            runOnUiThread([=, this] {
               ProfileService::RemoveUnavailable(profileIDs);
            });
        }
        MW_show_log(isUrl ? MainWindowTr::tr("URL test finished!") : MainWindowTr::tr("IP test finished!"));
    });
    return true;
}

bool TestRunner::runSpeedTests(const QList<int>& requestedIDs, bool testCurrent,
                               const std::function<void()>& onFinished, bool interactive)
{
    const auto finish = [onFinished] { if (onFinished) onFinished(); };
    // A live-connection test stays valid for a selector: it measures whichever member carries traffic.
    const auto profileIDs = testCurrent ? requestedIDs : testRunnerWithoutAutoSelectors(requestedIDs);
    if (profileIDs.isEmpty() && !testCurrent) {
        finish();
        return false;
    }
    if (!gate_.beginSpeed()) {
        const auto text = MainWindowTr::tr("The last test did not finish completely, please wait. If it persists, please restart the program.");
        if (interactive) policy_->testSessionBusy(text);
        else MW_show_log(text);
        finish();
        return false;
    }
    sessionGen_.fetch_add(1);
    gate_.stopRequested.store(false);
    interactive_.store(interactive);

    testingCurrent_.store(testCurrent);
    const QList<int> speedIDs = testCurrent && runningProfileId() != -1 ? QList<int>{runningProfileId()} : profileIDs;
    runOnUiThread([this, speedIDs] { observer_->testStateChanged(speedIDs, 2); });

    runOnNewThread([this, profileIDs, speedIDs, testCurrent, finish]() {
        { QMutexLocker lk(&creditMu_); credited_.clear(); }
        QHash<int, QList<int>> tested;
        if (!testCurrent)
        {
            observer_->testPanelSeeded(TestKind::Speed, profileIDs.size());
            observer_->testViewChanged(true);
            auto runBatch = [this](const QList<std::shared_ptr<Configs::Profile>>& profileSlice) {
                auto buildObject = Configs::BuildTestConfig(profileSlice);
                if (!buildObject->error.isEmpty()) {
                    MW_show_log(MainWindowTr::tr("Failed to build batch test config: ") + buildObject->error);
                    return;
                }

                for (auto it = buildObject->fullConfigs.cbegin(); it != buildObject->fullConfigs.cend(); ++it) {
                    Target target;
                    target.coreConfig = it.value();
                    target.useDefaultOutbound = true;
                    target.entID = it.key();
                    target.qwdttConfig = buildObject->qwdttConfigs.value(it.key());
                    target.testCurrent = !target.qwdttConfig.isEmpty() && runningProfileId() == it.key();
                    target.tag2entID.insert("proxy", it.key());
                    runSpeedProbe(target);
                }

                if (!buildObject->outboundTags.empty()) {
                    Target target;
                    target.coreConfig = QJsonObject2QString(buildObject->coreConfig, false);
                    target.xrayConfig = buildObject->isXrayNeeded ? QJsonObject2QString(buildObject->xrayConfig, true) : "";
                    target.xrayFullConfigs = buildObject->xrayFullConfigs;
                    target.outboundTags = buildObject->outboundTags;
                    target.tag2entID = buildObject->tag2entID;
                    target.xrayDnsStrategy = buildObject->xrayDnsStrategy;
                    runSpeedProbe(target);
                }
            };
            const int stepSize = Configs::dataManager->settingsRepo->speed_test_mode == Configs::TestConfig::COUNTRY ? kTestRunnerBatchSize : 1;
            for (int i = 0; i < profileIDs.length(); i += stepSize) {
                if (gate_.stopRequested.load()) break;
                const auto profileIDsSlice = profileIDs.mid(i, stepSize);
                auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(profileIDsSlice);
                for (const auto& profile : profiles) tested[profile->gid] << profile->id;
                runBatch(profiles);
            }
        } else
        {
            observer_->testPanelSeeded(TestKind::Speed, 1);
            Target target;
            target.testCurrent = true;
            runSpeedProbe(target);
            testingCurrent_.store(false);
            if (const auto running = runningSnapshot_()) tested[running->gid] << running->id;
        }
        // A speed test also credits the traffic it moved.
        Configs::RepositionLater(tested, Configs::SortInput::TestResult);
        Configs::RepositionLater(tested, Configs::SortInput::Traffic);
        sessionGen_.fetch_add(1);
        observer_->testPanelsCleared();
        observer_->testViewChanged(true);
        runOnUiThread([this, speedIDs] { observer_->testStateChanged(speedIDs, 0); }, true);
        gate_.finishSpeed();
        finish();
        runOnUiThread([=,this]{
            observer_->testedProfilesChanged(profileIDs);
            MW_show_log(MainWindowTr::tr("Speedtest finished!"));
        });
    });
    return true;
}

void TestRunner::creditTraffic(const std::shared_ptr<Configs::Profile>& profile, const QString& tag, qint64 curUp, qint64 curDown)
{
    if (profile == nullptr || tag.isEmpty()) return;
    if (Configs::dataManager->settingsRepo->disable_traffic_stats) return;
    QMutexLocker lk(&creditMu_);
    auto& base = credited_[tag];
    const qint64 dUp = curUp >= base.first ? curUp - base.first : curUp;
    const qint64 dDown = curDown >= base.second ? curDown - base.second : curDown;
    base = qMakePair(curUp, curDown);
    if (dUp <= 0 && dDown <= 0) return;

    Stats::trafficStatsManager->AddConfigDelta(profile->id, dUp, dDown);
    Stats::trafficStatsManager->AddAppDelta(Stats::SPEEDTEST_APP_NAME, "", dUp, dDown);

    profile->traffic_uplink += dUp;
    profile->traffic_downlink += dDown;
    Configs::dataManager->profilesRepo->SaveTraffic(profile);
}

void TestRunner::pollSpeedTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen)
{
    bool ok = false;
    const auto res = API::defaultClient->QueryCurrentSpeedTests(&ok);
    if (staleGen(gen)) return;
    if (!ok || !res.is_running.value())
    {
        return;
    }
    const libcore::SpeedTestResult result = res.result.value();
    const auto tag = QString::fromStdString(result.outbound_tag.value());
    // value(tag, -1), not operator[]: a const QMap yields 0 for a missing key.
    auto profile = testCurrent ? runningSnapshot_()
                               : Configs::dataManager->profilesRepo->GetProfile(tag2entID.value(tag, -1));
    if (profile == nullptr)
    {
        return;
    }
    creditTraffic(profile, tag, result.ul_bytes.value(), result.dl_bytes.value());
    runOnUiThread([this, profile, result, gen]
    {
        if (staleGen(gen)) return;
        const auto country = CountryNameToCode(QString::fromStdString(result.server_country.value()));
        QString liveText = profile->latency > 0 ? QString("%1 ms ").arg(profile->latency) : QString();
        if (!country.isEmpty()) liveText += CountryCodeToFlag(country);
        if (!result.dl_speed.value().empty()) liveText += " ↓" + QString::fromStdString(result.dl_speed.value());
        if (!result.ul_speed.value().empty()) liveText += " ↑" + QString::fromStdString(result.ul_speed.value());
        const bool measuring = !result.server_name.value().empty() || !result.dl_speed.value().empty() || !result.ul_speed.value().empty();
        observer_->testStateChanged({profile->id}, 2, country.isEmpty() && measuring ? QStringLiteral("unknown") : country, liveText);
        observer_->speedTestProgress(profile->outbound->name, result);
        observer_->testViewChanged(false);

        if (result.error.value().empty() && !result.cancelled.value())
        {
            if (!result.dl_speed.value().empty()) profile->dl_speed = QString::fromStdString(result.dl_speed.value());
            if (!result.ul_speed.value().empty()) profile->ul_speed = QString::fromStdString(result.ul_speed.value());
            if (profile->latency <= 0 && result.latency.value() > 0) profile->SetLatency(result.latency.value());
            if (!result.server_country.value().empty()) profile->test_country = CountryNameToCode(QString::fromStdString(result.server_country.value()));
            observer_->testedProfilesChanged({profile->id});
        }
    });
}

void TestRunner::pollCountryTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen)
{
    bool ok = false;
    const auto res = API::defaultClient->QueryCountryTestResults(&ok);
    if (staleGen(gen)) return;
    if (!ok || res.results.empty())
    {
        return;
    }
    for (const auto& result : res.results)
    {
        observer_->testProgressed();
        observer_->testViewChanged(false);
        const auto tag = QString::fromStdString(result.outbound_tag.value());
        auto profile = testCurrent ? runningSnapshot_()
                                   : Configs::dataManager->profilesRepo->GetProfile(tag2entID.value(tag, -1));
        if (profile == nullptr)
        {
            continue;
        }
        runOnUiThread([this, profile, result]
        {
            if (result.error.value().empty() && !result.cancelled.value())
            {
                if (profile->latency <= 0 && result.latency.value() > 0) profile->SetLatency(result.latency.value());
                if (!result.server_country.value().empty()) profile->test_country = CountryNameToCode(QString::fromStdString(result.server_country.value()));
                observer_->testedProfilesChanged({profile->id});
            }
        });
    }
    observer_->testViewChanged(true);
}

void TestRunner::runSpeedProbe(const Target& target)
{
    if (gate_.stopRequested.load()) {
        MW_show_log(MainWindowTr::tr("Profile speed test aborted"));
        return;
    }

    sessionGen_.fetch_add(1);

    const auto speedtestConf = Configs::dataManager->settingsRepo->speed_test_mode;
    libcore::SpeedTestRequest req;
    testRunnerFillCommonTestReq(req, target);
    req.test_download = speedtestConf == Configs::TestConfig::FULL || speedtestConf == Configs::TestConfig::DL;
    req.test_upload = speedtestConf == Configs::TestConfig::FULL || speedtestConf == Configs::TestConfig::UL;
    req.simple_download = speedtestConf == Configs::TestConfig::SIMPLEDL;
    req.simple_download_addr = Configs::dataManager->settingsRepo->simple_dl_url.toStdString();
    req.test_current = target.testCurrent;
    req.timeout_ms = Configs::dataManager->settingsRepo->speed_test_timeout_ms;
    req.only_country = speedtestConf == Configs::TestConfig::COUNTRY;
    req.country_concurrency = Configs::dataManager->settingsRepo->test_concurrent;

    if (speedtestConf != Configs::TestConfig::COUNTRY) {
        observer_->testProgressed();
        observer_->testViewChanged(false);
    }

    const int contextID = target.testCurrent ? runningProfileId() : target.entID;

    bool rpcOK = false;
    QString coreError;
    libcore::SpeedTestResponse result;
    {
        TestRunnerResultPoller poller([this, gen = sessionGen_.load(), tag2entID = target.tag2entID, testCurrent = target.testCurrent, speedtestConf] {
            if (staleGen(gen)) return;
            if (speedtestConf == Configs::TestConfig::COUNTRY) pollCountryTest(tag2entID, testCurrent, gen);
            else pollSpeedTest(tag2entID, testCurrent, gen);
        }, kTestRunnerSpeedPollIntervalMs);

        result = API::defaultClient->SpeedTest(&rpcOK, req, &coreError);
    }

    if (!rpcOK || result.results.empty()) {
        if (!rpcOK) AssetUpdates::HandleXrayGeoError(policy_, coreError, contextName(contextID), interactive_.load());
        return;
    }

    for (const auto& res : result.results) {
        const int entid = target.testCurrent
                              ? runningProfileId()
                              : testRunnerResolveEntID(target.tag2entID, res.outbound_tag.value(), target.entID);
        if (entid == -1) {
            MW_show_log(MainWindowTr::tr("Something is very wrong, the subject ent cannot be found!"));
            continue;
        }

        auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
        if (ent == nullptr) {
            MW_show_log(MainWindowTr::tr("Profile manager data is corrupted, try again."));
            continue;
        }

        creditTraffic(ent, QString::fromStdString(res.outbound_tag.value()),
                      res.ul_bytes.value(), res.dl_bytes.value());

        if (res.cancelled.value()) continue;

        const auto error = QString::fromStdString(res.error.value());
        if (error.isEmpty()) {
            ent->dl_speed = QString::fromStdString(res.dl_speed.value());
            ent->ul_speed = QString::fromStdString(res.ul_speed.value());
            if (ent->latency <= 0 && res.latency.value() > 0) ent->SetLatency(res.latency.value());
            if (!res.server_country.value().empty()) ent->test_country = CountryNameToCode(QString::fromStdString(res.server_country.value()));
        } else {
            ent->dl_speed = "N/A";
            ent->ul_speed = "N/A";
            ent->SetLatency(error.startsWith("qWDTT test not started:") ? 0 : -1);
            ent->test_country = "";
            MW_show_log(MainWindowTr::tr("[%1] speed test error: %2").arg(ent->outbound->DisplayTypeAndName(), error));
        }
        Configs::dataManager->profilesRepo->Save(ent);
        runOnUiThread([this, entid] { observer_->testStateChanged({entid}, 0); });
    }
}
} // namespace Engine
