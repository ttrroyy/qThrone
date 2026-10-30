#pragma once
#include <mutex>
#include <condition_variable>
#include <set>
#include <atomic>
#include <algorithm>

// Latency runs share the gate; speed tests own it exclusively, including queued work.
class TestSessionGate {
public:
    enum class Start { Started, Busy, Empty };
    std::atomic<bool> stopRequested = false;
    template<class List> Start beginLatency(List &ids, bool wait = false) {
        std::unique_lock lock(mutex_);
        while (speed_ || (stopRequested.load() && !ids_.empty()) ||
               (wait && std::any_of(ids.begin(), ids.end(), [this](int id) { return ids_.contains(id); }))) {
            if (!wait) return Start::Busy;
            changed_.wait(lock);
        }
        const bool first = ids_.empty();
        List fresh;
        for (int id : ids) if (!ids_.contains(id)) { ids_.insert(id); fresh.push_back(id); }
        ids = fresh;
        if (ids.empty()) return Start::Empty;
        if (first) stopRequested.store(false);
        return Start::Started;
    }
    bool beginSpeed() {
        std::unique_lock lock(mutex_);
        if (speed_ || !ids_.empty()) return false;
        speed_ = true;
        stopRequested.store(false);
        return true;
    }
    template<class List> bool finishLatency(const List &ids) {
        std::unique_lock lock(mutex_);
        for (int id : ids) ids_.erase(id);
        changed_.notify_all();
        return ids_.empty();
    }
    void finishSpeed() {
        std::unique_lock lock(mutex_);
        speed_ = false;
        changed_.notify_all();
    }
    bool running() {
        std::unique_lock lock(mutex_);
        return speed_ || !ids_.empty();
    }
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::set<int> ids_;
    bool speed_ = false;
};
