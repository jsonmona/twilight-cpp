#include "NetworkClock.h"

using namespace std::chrono_literals;

static constexpr uint64_t PANIC_THRESHOLD = 5'000'000;  // 5 sec
static constexpr uint64_t MINIMUM_THRESHOLD = 1'000;    // 1 ms

NetworkClock::NetworkClock()
    : log(createNamedLogger("NetworkClock")),
      networkLatency(0),
      networkJitter(0),
      epoch(std::chrono::steady_clock::now()),
      lastPing(std::chrono::steady_clock::now()),
      random(std::random_device()()) {}

NetworkClock::~NetworkClock() {}

std::chrono::microseconds NetworkClock::time() const {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - epoch);
}

int NetworkClock::latency() const {
    return networkLatency;
}

int NetworkClock::jitter() const {
    return networkJitter;
}

void NetworkClock::adjust(uint32_t pingId, uint64_t clock) {
    std::lock_guard lk(lock);

    auto itr = pingReqMap.find(pingId);
    if (itr == pingReqMap.end())
        return;

    std::chrono::steady_clock::time_point prevClock = itr->second;
    pingReqMap.erase(itr);

    std::chrono::steady_clock::time_point currClock = std::chrono::steady_clock::now();
    long long ping = std::chrono::duration_cast<std::chrono::microseconds>(currClock - prevClock).count();

    if (networkLatency == 0)
        networkLatency = ping;
    else
        networkLatency += (ping - networkLatency) / 2;

    // Zero means no estimation
    if (networkLatency == 0)
        networkLatency = 1;

    std::chrono::microseconds remoteClock = std::chrono::microseconds(clock - ping / 2);
    std::chrono::microseconds localClock =
        std::chrono::duration_cast<std::chrono::microseconds>(currClock - epoch) - std::chrono::microseconds(ping / 2);
    std::chrono::microseconds diff = remoteClock - localClock;
    long long absDiff = std::abs(diff.count());

    if (PANIC_THRESHOLD <= absDiff)
        epoch -= diff;
    else if (MINIMUM_THRESHOLD <= absDiff)
        epoch -= diff / 2;
}

uint32_t NetworkClock::generatePing() {
    std::lock_guard lk(lock);

    if (networkLatency == 0 && pingReqMap.size() < 3)
        return sendPing_();

    if (5s <= std::chrono::steady_clock::now() - lastPing)
        return sendPing_();

    return 0;
}

uint32_t NetworkClock::sendPing_() {
    std::chrono::steady_clock::time_point currTime = std::chrono::steady_clock::now();
    lastPing = currTime;

    std::vector<uint32_t> toRemove;
    for (auto& itr : pingReqMap) {
        if (30s < currTime - itr.second)
            toRemove.push_back(itr.first);
    }

    for (uint32_t key : toRemove)
        pingReqMap.erase(key);

    uint32_t id = random();
    pingReqMap[id] = currTime;
    return id;
}
