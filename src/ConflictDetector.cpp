#include "ConflictDetector.h"
#include <algorithm>

bool ConflictDetector::hasConflict(const UserCalendar& cal, const TimeSlot& slot) {
    return !cal.isFree(slot);
}

std::vector<Event> ConflictDetector::conflicts(const UserCalendar& cal, const TimeSlot& slot) {
    return cal.conflictsWith(slot);
}

static std::vector<TimeSlot> intersect(const std::vector<TimeSlot>& a, const std::vector<TimeSlot>& b) {
    std::vector<TimeSlot> out;
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        int lo = std::max(a[i].start(), b[j].start());
        int hi = std::min(a[i].end(), b[j].end());
        if (lo < hi) out.emplace_back(lo, hi);
        if (a[i].end() < b[j].end()) ++i; else ++j;
    }
    return out;
}

std::optional<TimeSlot> ConflictDetector::findCommonFreeSlot(
    const std::vector<const UserCalendar*>& calendars, int duration, int dayStart, int dayEnd) {
    if (calendars.empty()) return std::nullopt;
    std::vector<TimeSlot> common = calendars[0]->freeSlots(dayStart, dayEnd);
    for (std::size_t k = 1; k < calendars.size(); ++k)
        common = intersect(common, calendars[k]->freeSlots(dayStart, dayEnd));
    for (const TimeSlot& s : common)
        if (s.duration() >= duration) return TimeSlot(s.start(), s.start() + duration);
    return std::nullopt;
}
