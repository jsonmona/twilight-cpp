#ifndef TWILIGHT_CLIENT_NETWORKCLOCK_H
#define TWILIGHT_CLIENT_NETWORKCLOCK_H

#include "common/StatisticMixer.h"
#include "common/log.h"

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <random>

class NetworkClock {
public:
    NetworkClock();
    ~NetworkClock();

    std::chrono::microseconds time() const;
    int latency() const;
    int jitter() const;

    void adjust(uint32_t pingId, uint64_t clock);

    // Hint that current time must be larger or equal to `clockLeast`
    void monotonicHint(std::chrono::microseconds clockLeast);
    void monotonicHint(uint64_t clockLeast) { monotonicHint(std::chrono::microseconds(clockLeast)); }

    // Return value: whether to send ping
    // [out] pingId: Specifies the id (only valid when return value is true)
    // [out] sleepAmount: Specifies how long to sleep (always valid)
    bool generatePing(uint32_t* pingId, std::chrono::milliseconds* sleepAmount);

private:
    static NamedLogger log;

    std::atomic<std::chrono::steady_clock::time_point::rep> epoch;

    std::mutex lock;
    int networkLatency;
    int networkJitter;
    size_t pingTimingIdx;
    std::chrono::steady_clock::time_point lastPing;

    std::map<uint32_t, std::chrono::steady_clock::time_point> pingReqMap;
    std::default_random_engine random;

    uint32_t sendPing_();
};

#endif
