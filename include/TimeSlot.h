#pragma once
#include <string>

// Half-open interval [start, end) measured in minutes from midnight.
// Encapsulates validation so an invalid slot can never exist.
class TimeSlot {
public:
    TimeSlot(int start, int end);

    int start() const { return start_; }
    int end() const { return end_; }
    int duration() const { return end_ - start_; }

    bool overlaps(const TimeSlot& other) const;
    bool contains(const TimeSlot& other) const;
    std::string toString() const;  // "09:00-10:30"

    bool operator==(const TimeSlot& o) const { return start_ == o.start_ && end_ == o.end_; }

private:
    int start_;
    int end_;
};

