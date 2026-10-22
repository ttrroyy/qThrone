#include "include/ui/mainWindow/TestSessionGate.h"
#include <cassert>
#include <future>
#include <vector>
#include <chrono>
int main() {
    TestSessionGate gate;
    std::vector<int> first{1,2}, second{2,3}, duplicate{1,2,3};
    assert(gate.beginLatency(first) == TestSessionGate::Start::Started);
    assert(gate.beginLatency(second) == TestSessionGate::Start::Started);
    assert(second == std::vector<int>{3});
    assert(gate.beginLatency(duplicate) == TestSessionGate::Start::Empty);
    assert(!gate.beginSpeed());
    std::vector<int> overlapping{1};
    auto overlap = std::async(std::launch::async, [&] { return gate.beginLatency(overlapping, true); });
    assert(overlap.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
    assert(!gate.finishLatency(first));
    assert(overlap.get() == TestSessionGate::Start::Started);
    assert(!gate.finishLatency(overlapping));
    assert(gate.running());
    gate.stopRequested.store(true);
    std::vector<int> blocked{4};
    assert(gate.beginLatency(blocked) == TestSessionGate::Start::Busy);
    assert(gate.finishLatency(second));
    assert(gate.beginSpeed());
    assert(!gate.beginSpeed());
    assert(gate.beginLatency(blocked) == TestSessionGate::Start::Busy);
    auto waiting = std::async(std::launch::async, [&] { return gate.beginLatency(blocked, true); });
    assert(waiting.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
    gate.finishSpeed();
    assert(waiting.get() == TestSessionGate::Start::Started);
    assert(!gate.stopRequested.load());
    assert(gate.finishLatency(blocked));
    assert(!gate.running());
}
