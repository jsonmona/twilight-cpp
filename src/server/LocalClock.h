#ifndef TWILIGHT_SERVER_LOCALCLOCK_H
#define TWILIGHT_SERVER_LOCALCLOCK_H

#include <chrono>

class LocalClock {
public:
    LocalClock();
    ~LocalClock();

    std::chrono::microseconds time() const;

private:
    std::chrono::steady_clock::time_point epoch;
};

#endif
