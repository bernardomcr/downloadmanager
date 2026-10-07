#include "core/rate_limiter.h"

#include <algorithm>

namespace dm {
namespace {

// Rajada máxima: um quarto de segundo de limite (no mínimo 16 KB), para não "economizar" velocidade
// enquanto parado e depois estourar o limite.
double burstFor(int64_t rate) {
    return std::max(static_cast<double>(rate) / 4.0, 16.0 * 1024.0);
}

}  // namespace

void RateLimiter::setRate(int64_t bytesPerSecond) {
    std::lock_guard lock(mutex_);
    rate_ = std::max<int64_t>(bytesPerSecond, 0);
    started_ = false;
}

int64_t RateLimiter::rate() const {
    std::lock_guard lock(mutex_);
    return rate_;
}

std::chrono::nanoseconds RateLimiter::consume(int64_t bytes, Clock::time_point now) {
    std::lock_guard lock(mutex_);
    if (rate_ <= 0) return std::chrono::nanoseconds::zero();

    const double burst = burstFor(rate_);
    if (!started_) {
        available_ = burst;
        last_ = now;
        started_ = true;
    }
    const double elapsed = std::chrono::duration<double>(now - last_).count();
    if (elapsed > 0) {
        available_ = std::min(burst, available_ + elapsed * static_cast<double>(rate_));
        last_ = now;
    }

    available_ -= static_cast<double>(bytes);
    if (available_ >= 0) return std::chrono::nanoseconds::zero();
    const double seconds = -available_ / static_cast<double>(rate_);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(seconds));
}

bool scheduleAllows(bool enabled, int startMinute, int endMinute, int nowMinute) {
    if (!enabled || startMinute == endMinute) return true;
    if (startMinute < endMinute) return nowMinute >= startMinute && nowMinute < endMinute;
    return nowMinute >= startMinute || nowMinute < endMinute;
}

}  // namespace dm
