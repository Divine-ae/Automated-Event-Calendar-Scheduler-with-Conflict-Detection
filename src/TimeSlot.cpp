#include "TimeSlot.h"
#include <cstdio>
#include <stdexcept>

TimeSlot::TimeSlot(int start, int end) : start_(start), end_(end) {
    if (start < 0 || end <= start)
        throw std::invalid_argument("TimeSlot: need 0 <= start < end");
}

bool TimeSlot::overlaps(const TimeSlot& o) const {
    return start_ < o.end_ && o.start_ < end_;
}

bool TimeSlot::contains(const TimeSlot& o) const {
    return start_ <= o.start_ && o.end_ <= end_;
}

std::string TimeSlot::toString() const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d-%02d:%02d",
                  start_ / 60, start_ % 60, end_ / 60, end_ % 60);
    return buf;
}
