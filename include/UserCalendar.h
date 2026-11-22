#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "Event.h"
#include "IntervalTree.h"

// One user's calendar. Events are kept in a map (by id) and indexed
// in an interval tree for fast conflict checks.
class UserCalendar {
public:
    explicit UserCalendar(std::string owner) : owner_(std::move(owner)) {}

    const std::string& owner() const { return owner_; }

    bool addEvent(const Event& e);                 // false if it conflicts
    bool removeEvent(int id);
    bool moveEvent(int id, const TimeSlot& newSlot); // false if conflict / not found
    const Event* getEvent(int id) const;
    std::vector<Event> events() const;             // sorted by start time

    bool isFree(const TimeSlot& slot) const;
    std::vector<Event> conflictsWith(const TimeSlot& slot) const;
    std::vector<TimeSlot> freeSlots(int dayStart, int dayEnd) const;
    std::optional<TimeSlot> firstFreeSlot(int duration, int from, int to) const;

private:
    std::string owner_;
    std::map<int, Event> events_;
    IntervalTree tree_;
};
