#ifndef TWILIGHT_COMMON_STATISTICMIXER_H
#define TWILIGHT_COMMON_STATISTICMIXER_H

#include "common/log.h"

#include <cstdint>
#include <vector>

class StatisticMixer {
public:
    struct Stat {
        float min, avg, max, stddev;

        bool valid() const { return !isnan(avg); }
    };

    explicit StatisticMixer(size_t initialSize = 0);
    StatisticMixer(size_t initialSize, float initialValue);
    ~StatisticMixer();

    void setPoolSize(int samples);

    void pushValue(float val);
    Stat calcStat(bool skipNaN = false);

private:
    static NamedLogger log;

    std::vector<float> arr;
    size_t arrIdx;
};

#endif
