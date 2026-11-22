#include "UserCalendar.h"
#include <algorithm>

bool UserCalendar::addEvent(const Event& e) {
    if (events_.count(e.id()) || tree_.anyOverlap(e.slot())) return false;
    events_.emplace(e.id(), e);
    tree_.insert(e.slot(), e.id());
    return true;
}

bool UserCalendar::removeEvent(int id) {
    auto it = events_.find(id);
    if (it == events_.end()) return false;
    tree_.remove(it->second.slot(), id);
    events_.erase(it);
    return true;
}

bool UserCalendar::moveEvent(int id, const TimeSlot& newSlot) {
    auto it = events_.find(id);
    if (it == events_.end()) return false;
    TimeSlot old = it->second.slot();
    tree_.remove(old, id);
    if (tree_.anyOverlap(newSlot)) {  // roll back
        tree_.insert(old, id);
        return false;
    }
    it->second.setSlot(newSlot);
    tree_.insert(newSlot, id);
    return true;
}

const Event* UserCalendar::getEvent(int id) const {
    auto it = events_.find(id);
    return it == events_.end() ? nullptr : &it->second;
}

std::vector<Event> UserCalendar::events() const {
    std::vector<Event> v;
    for (const auto& kv : events_) v.push_back(kv.second);
    std::sort(v.begin(), v.end(), [](const Event& a, const Event& b) {
        return a.slot().start() < b.slot().start();
    });
    return v;
}

bool UserCalendar::isFree(const TimeSlot& slot) const {
    return !tree_.anyOverlap(slot);
}

std::vector<Event> UserCalendar::conflictsWith(const TimeSlot& slot) const {
    std::vector<Event> v;
    for (int id : tree_.findOverlaps(slot)) v.push_back(events_.at(id));
    return v;
}

std::vector<TimeSlot> UserCalendar::freeSlots(int dayStart, int dayEnd) const {
    std::vector<TimeSlot> out;
    int cursor = dayStart;
    for (const Event& e : events()) {
        if (e.slot().end() <= dayStart) continue;
        if (e.slot().start() >= dayEnd) break;
        if (e.slot().start() > cursor) out.emplace_back(cursor, e.slot().start());
        cursor = std::max(cursor, e.slot().end());
    }
    if (cursor < dayEnd) out.emplace_back(cursor, dayEnd);
    return out;
}

std::optional<TimeSlot> UserCalendar::firstFreeSlot(int duration, int from, int to) const {
    for (const TimeSlot& s : freeSlots(from, to))
        if (s.duration() >= duration) return TimeSlot(s.start(), s.start() + duration);
    return std::nullopt;
}
