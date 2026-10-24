#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QThreadPool>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

#ifndef Q_MOC_RUN
#include <core/gen/libcore.pb.h>
#endif

#include "include/database/entities/Profile.h"
#include "TestSessionGate.h"

class MainWindow;

// Not a QObject: queued signals would reorder the synchronous progress path.
class TestRunner {
public:
    explicit TestRunner(MainWindow* mw) : mw_(mw) {
        nativeProbePool_.setMaxThreadCount(3);
        vkProbePool_.setMaxThreadCount(12);
    }

    TestRunner(const TestRunner&) = delete;
    TestRunner& operator=(const TestRunner&) = delete;

    // The run* calls return whether a session started; onFinished fires either way, on a refusal before they return.
    bool runUrlTests(const QList<int>& profileIDs, const std::function<void()>& onFinished = {}, bool interactive = true);

    // Waits for speed tests or overlapping profile IDs; safe from any thread.
    void queueUrlTests(const QList<int>& profileIDs, const std::function<void()>& onFinished);

    bool runIpTests(const QList<int>& profileIDs, const std::function<void()>& onFinished = {}, bool interactive = true);

    bool runSpeedTests(const QList<int>& profileIDs, bool testCurrent = false,
                       const std::function<void()>& onFinished = {}, bool interactive = true);

    void stop();

    bool isRunning();

    bool isTestingCurrent() const { return testingCurrent_.load(); }

    // Whether stop() was called since the latest session started.
    bool stopRequested() const { return gate_.stopRequested.load(); }

private:
    enum class LatencyKind { Url, Ip };

    struct Target {
        QString coreConfig;
        QString xrayConfig;
        QStringList xrayFullConfigs;
        QStringList outboundTags;
        QMap<QString, int> tag2entID;
        QString xrayDnsStrategy;
        QString qwdttConfig;
        int entID = -1;
        // Not derivable from an empty outboundTags: a test-current run leaves both empty but wants "proxy".
        bool useDefaultOutbound = false;
        bool testCurrent = false;
    };

    bool runLatencyGroup(LatencyKind kind, const QList<int>& requestedIDs,
                         const std::function<void()>& onFinished, bool waitForSession = false, bool interactive = true);

    void runUrlProbe(const Target& target);

    void runIpProbe(const Target& target);

    void runSpeedProbe(const Target& target);

    void applyUrlResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::URLTestResp& res,
                        const QHash<QString, bool>* vpnConnected = nullptr);

    void applyIpResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::IPTestRes& res);

    QString contextName(int entID) const;

    bool staleGen(quint64 gen) const { return sessionGen_.load() != gen; }

    void pollSpeedTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen);

    void pollCountryTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen);

    void creditTraffic(const std::shared_ptr<Configs::Profile>& profile, const QString& tag,
                       qint64 curUp, qint64 curDown);

    MainWindow* mw_;

    // Multiple latency sweeps share ownership; speed tests remain exclusive.
    TestSessionGate gate_;
    // A poll thread is not joined, so a late tick must not drain the next batch.
    std::atomic<quint64> sessionGen_ = 0;
    std::atomic<bool> testingCurrent_ = false;
    // Counts stop() calls; each probe sends the count it launched under (see StopTestReq).
    std::atomic<qint64> stopEpoch_ = 0;

    // Tests bypass the clash tracker, so their bytes are counted only here, diffed per tag.
    QMutex creditMu_;
    QHash<QString, QPair<qint64, qint64>> credited_;
    // Streamed URL results: tags of every running shared-box probe (unique per profile across concurrent
    // sweeps, as the gate never tests one profile twice) and the tags already applied from the stream.
    QMutex streamMu_;
    QHash<QString, int> streamTags_;
    QSet<QString> streamed_;
    void pollUrlStream();
    // Profiles of running URL sweeps, so a repeated press can restart them.
    QMutex urlMu_;
    QSet<int> urlIds_;
    // UI thread only: restarted rows keep their spinner while the stopped sweep reports its aborts.
    QSet<int> restarting_;
    void clearTestState(const QList<int>& ids);

    // Destroyed first: wait for native workers before their owner state disappears.
    QThreadPool nativeProbePool_;
    // qWDTT, CSQTT and WDTT Plus: the core queues them per device and per VK call and runs three at a time,
    // so a probe waiting there for its turn must not hold a thread another native profile needs.
    QThreadPool vkProbePool_;
};
