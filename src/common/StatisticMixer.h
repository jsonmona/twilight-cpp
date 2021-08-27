#ifndef COMMON_STATISTIC_MIXER_H_
#define COMMON_STATISTIC_MIXER_H_


#include <cstdint>
#include <vector>

#include "common/log.h"


class StatisticMixer {
public:
	struct Stat {
		float min, avg, max;

		bool valid() const {
			return !isnan(avg);
		}
	};

	explicit StatisticMixer(size_t initialSize = 0);
	~StatisticMixer();

	void setPoolSize(int samples);

	void pushValue(float val);
	Stat calcStat();

private:
	LoggerPtr log;

	std::vector<float> arr;
	size_t arrIdx;
};


#endif