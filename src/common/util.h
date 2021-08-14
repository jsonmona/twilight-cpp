#ifndef COMMON_UTIL_H_
#define COMMON_UTIL_H_


#include <atomic>
#include <type_traits>


// From https://stackoverflow.com/a/21298525
template<typename T,
	typename = typename std::enable_if<std::is_integral<T>::value>::type,
	typename = typename std::enable_if<std::is_unsigned<T>::value>::type>
	constexpr T constexpr_nextPowerOfTwo(
		T value,
		uintmax_t maxb = sizeof(T) * CHAR_BIT,
		uintmax_t curb = 1
	) {
	return maxb <= curb
		? value
		: constexpr_nextPowerOfTwo(((value - 1) | ((value - 1) >> curb)) + 1, maxb, curb << 1);
}


#if defined(ATOMIC_BOOL_LOCK_FREE) && ATOMIC_BOOL_LOCK_FREE >= 1
class spinlock {
public:
	spinlock() : af(false) {}
	spinlock(const spinlock& copy) = delete;
	spinlock(spinlock&& move) = delete;

	spinlock& operator=(const spinlock& copy) = delete;
	spinlock&& operator=(spinlock&& move) = delete;

	void lock() noexcept {
		while(true) {
			if (!af.exchange(true, std::memory_order_acquire)) {
				return;
			}

			while (af.load(std::memory_order_relaxed)) {
				_mm_pause();  // __builtin_ia32_pause on GCC
			}
		}
	}

	bool try_lock() noexcept {
		return !af.load(std::memory_order_relaxed) &&
			!af.exchange(true, std::memory_order_acquire);
	}

	void unlock() noexcept {
		af.store(false, std::memory_order_release);
	}

private:
	std::atomic<bool> af;
};
#else
class spinlock {
public:
	spinlock() {}
	spinlock(const spinlock& copy) = delete;
	spinlock(spinlock&& move) = delete;

	spinlock& operator=(const spinlock& copy) = delete;
	spinlock&& operator=(spinlock&& move) = delete;

	void lock() noexcept {
		while (af.test_and_set(std::memory_order_acquire))
			_mm_pause();
	}

	bool try_lock() noexcept {
		return !af.test_and_set(std::memory_order_acquire);
	}

	void unlock() noexcept {
		af.clear(std::memory_order_release);
	}

private:
	std::atomic_flag af = ATOMIC_FLAG_INIT;
};
#endif


#endif