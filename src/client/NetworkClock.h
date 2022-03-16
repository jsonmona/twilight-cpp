#ifndef TWILIGHT_CLIENT_NETWORKCLOCK_H
#define TWILIGHT_CLIENT_NETWORKCLOCK_H

#include <chrono>
#include <map>
#include <mutex>
#include <random>

#include "common/log.h"

class NetworkClock {
public:
    NetworkClock();
    ~NetworkClock();

    std::chrono::microseconds time() const;
    int latency() const;
    int jitter() const;

    void adjust(uint32_t pingId, uint64_t clock);

    // 0 => Do not send ping; Else => Specifies an id
    uint32_t generatePing();

private:
    LoggerPtr log;

    std::mutex lock;
    int networkLatency;
    int networkJitter;
    std::chrono::steady_clock::time_point epoch;
    std::chrono::steady_clock::time_point lastPing;

    std::map<uint32_t, std::chrono::steady_clock::time_point> pingReqMap;
    std::default_random_engine random;

    uint32_t sendPing_();
};

#endif