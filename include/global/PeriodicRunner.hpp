#pragma once

#include <QString>
#include <functional>
#include <vector>

class QTimer;

namespace Throne {

// The last-run time is persisted through setLastRun, so the schedule survives restarts and catches up on missed windows.
struct PeriodicTask {
    QString id;
    // <= 0 disables the job; read live on every tick, so a setting change needs no re-registration.
    std::function<int()> intervalMinutes;
    // Epoch-seconds; 0 = never.
    std::function<qint64()> lastRun;
    std::function<void(qint64)> setLastRun;
    // Runs on the tick (UI) thread, so it must return quickly.
    std::function<void()> run;
};

// Single app-wide instance living on the UI thread; register jobs once during startup.
class PeriodicRunner {
public:
    static PeriodicRunner* instance();

    // Eligible from the next tick on, including the short post-startup catch-up check.
    void Add(PeriodicTask task);

    void CheckNow();

private:
    PeriodicRunner();
    void tick();

    QTimer* m_timer = nullptr;
    std::vector<PeriodicTask> m_tasks;
};

} // namespace Throne
