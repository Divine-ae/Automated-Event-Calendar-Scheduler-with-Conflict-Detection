#pragma once
#include <optional>
#include <vector>
#include "UserCalendar.h"

// Conflict Detection Service: sits between the scheduler and the interval trees.
class ConflictDetector {
public:
    static bool hasConflict(const UserCalendar& cal, const TimeSlot& slot);
    static std::vector<Event> conflicts(const UserCalendar& cal, const TimeSlot& slot);

    // First slot of 'duration' minutes in [dayStart, dayEnd) where ALL calendars are free.
    static std::optional<TimeSlot> findCommonFreeSlot(
        const std::vector<const UserCalendar*>& calendars,
        int duration, int dayStart, int dayEnd);
};
