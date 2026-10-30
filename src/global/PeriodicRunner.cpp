#include "include/global/PeriodicRunner.hpp"

#include <QDateTime>
#include <QObject>
#include <QTimer>

#include "include/global/Utils.hpp"

namespace Throne {

static constexpr int kPollSeconds = 60;
// Lets an overdue job fire shortly after launch instead of a full interval later.
static constexpr int kInitialDelaySeconds = 10;

PeriodicRunner* PeriodicRunner::instance() {
    static auto* runner = new PeriodicRunner();
    return runner;
}

PeriodicRunner::PeriodicRunner() {
    // No parent: an app-lifetime singleton on the UI thread, so the timer keeps the thread with the event loop.
    m_timer = new QTimer();
    QObject::connect(m_timer, &QTimer::timeout, m_timer, [this] { tick(); });
    m_timer->start(kPollSeconds * 1000);
    QTimer::singleShot(kInitialDelaySeconds * 1000, m_timer, [this] { tick(); });
}

void PeriodicRunner::Add(PeriodicTask task) {
    m_tasks.push_back(std::move(task));
}

void PeriodicRunner::CheckNow() {
    tick();
}

void PeriodicRunner::tick() {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const auto& task : m_tasks) {
        const int minutes = task.intervalMinutes ? task.intervalMinutes() : 0;
        if (minutes <= 0) continue; // disabled
        const qint64 last = task.lastRun ? task.lastRun() : 0;
        // last == 0 means never run, which is always due.
        if (last > 0 && now - last < static_cast<qint64>(minutes) * 60) continue;
        // Recorded before the run, so a slow job cannot double-fire.
        if (task.setLastRun) task.setLastRun(now);
        if (!task.id.isEmpty()) MW_show_log(QObject::tr("Auto-update: running %1").arg(task.id));
        if (task.run) task.run();
    }
}

} // namespace Throne
