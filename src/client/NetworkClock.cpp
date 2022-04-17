#include "NetworkClock.h"

#include <type_traits>

TWILIGHT_DEFINE_LOGGER(NetworkClock);

using namespace std::chrono_literals;

static constexpr uint64_t PANIC_THRESHOLD = 300'000;  // 300 ms
static constexpr uint64_t MINIMUM_THRESHOLD = 100;    // 0.1 ms

NetworkClock::NetworkClock()
    : networkLatency(0),
      networkJitter(0),
      pingTimingIdx(0),
      epoch(std::chrono::steady_clock::now().time_since_epoch().count()),
      lastPing(std::chrono::steady_clock::now()),
      random(std::random_device()()) {}

NetworkClock::~NetworkClock() {}

std::chrono::microseconds NetworkClock::time() const {
    auto t = std::chrono::steady_clock::now().time_since_epoch();
    auto currEpoch = std::chrono::steady_clock::duration(epoch.load(std::memory_order_relaxed));
    return std::chrono::duration_cast<std::chrono::microseconds>(t - currEpoch);
}

int NetworkClock::latency() const {
    return networkLatency;
}

int NetworkClock::jitter() const {
    return networkJitter;
}

void NetworkClock::adjust(uint32_t pingId, uint64_t clock) {
    std::chrono::steady_clock::time_point currClock = std::chrono::steady_clock::now();

    monotonicHint(std::chrono::microseconds(clock));

    std::lock_guard lk(lock);

    auto currEpoch = std::chrono::steady_clock::duration(epoch.load(std::memory_order_relaxed));
    auto itr = pingReqMap.find(pingId);
    if (itr == pingReqMap.end())
        return;

    std::chrono::steady_clock::time_point prevClock = itr->second;
    pingReqMap.erase(itr);

    long long ping = std::chrono::duration_cast<std::chrono::microseconds>(currClock - prevClock).count();

    if (networkLatency == 0)
        networkLatency = ping;
    else
        networkLatency += (ping - networkLatency) / 2;

    // Zero means no estimation
    networkLatency = std::max(1, networkLatency);

    std::chrono::microseconds remoteClock = std::chrono::microseconds(clock - ping / 2);
    std::chrono::microseconds localClock =
        std::chrono::duration_cast<std::chrono::microseconds>(currClock.time_since_epoch() - currEpoch) -
        std::chrono::microseconds(ping / 2);

    std::chrono::microseconds diff = remoteClock - localClock;
    long long absDiff = std::abs(diff.count());
    log.debug("Ping diff by {} us", diff.count());

    auto adjustment = std::chrono::duration_cast<std::chrono::steady_clock::duration>(diff).count();
    if (PANIC_THRESHOLD <= absDiff) {
        epoch.fetch_sub(adjustment, std::memory_order_relaxed);
        pingTimingIdx = 0;
    } else if (MINIMUM_THRESHOLD <= absDiff)
        epoch.fetch_sub(adjustment / 2, std::memory_order_relaxed);
}

void NetworkClock::monotonicHint(std::chrono::microseconds clockLeast) {
    std::chrono::microseconds currTime = time();
    if (clockLeast <= currTime)
        return;

    std::lock_guard lk(lock);

    std::chrono::microseconds diff = clockLeast - time();
    auto adjustment = std::chrono::duration_cast<std::chrono::steady_clock::duration>(diff).count();

    epoch.fetch_sub(adjustment, std::memory_order_relaxed);
}

bool NetworkClock::generatePing(uint32_t* pingId, std::chrono::milliseconds* sleepAmount) {
    static constexpr std::chrono::milliseconds TIMING_TABLE[] = {1ms,   10ms,  10ms, 100ms, 300ms,
                                                                 300ms, 300ms, 5s,   5s,    25s};
    static constexpr size_t TIMING_LEN = sizeof(TIMING_TABLE) / sizeof(decltype(TIMING_TABLE[0]));

    std::lock_guard lk(lock);

    if (pingTimingIdx < 0)
        pingTimingIdx = 0;

    if (networkLatency == 0 && pingReqMap.empty()) {
        *sleepAmount = std::min(60ms, TIMING_TABLE[pingTimingIdx]);
        *pingId = sendPing_();
        return true;
    }

    if (TIMING_TABLE[pingTimingIdx] <= std::chrono::steady_clock::now() - lastPing) {
        if (pingTimingIdx < TIMING_LEN - 1)
            pingTimingIdx++;
        *sleepAmount = std::min(60ms, TIMING_TABLE[pingTimingIdx]);
        *pingId = sendPing_();
        return true;
    }

    *sleepAmount = 15ms;
    return false;
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
    auto& ref = pingReqMap[id];
    ref = std::chrono::steady_clock::now();
    return id;
}
