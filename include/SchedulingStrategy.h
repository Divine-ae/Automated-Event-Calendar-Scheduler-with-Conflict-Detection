#pragma once
#include "Task.h"

// Strategy interface: decides which task is more urgent.
class SchedulingStrategy {
public:
    virtual ~SchedulingStrategy() = default;
    virtual bool higherPriority(const Task& a, const Task& b) const = 0;
    virtual const char* name() const = 0;
};

// Earliest Deadline First.
class EarliestDeadlineFirst : public SchedulingStrategy {
public:
    bool higherPriority(const Task& a, const Task& b) const override {
        if (a.deadline != b.deadline) return a.deadline < b.deadline;
        if (a.priority != b.priority) return a.priority > b.priority;
        return a.id < b.id;
    }
    const char* name() const override { return "EDF"; }
};

// Shortest Duration First.
class ShortestDurationFirst : public SchedulingStrategy {
public:
    bool higherPriority(const Task& a, const Task& b) const override {
        if (a.duration != b.duration) return a.duration < b.duration;
        if (a.deadline != b.deadline) return a.deadline < b.deadline;
        return a.id < b.id;
    }
    const char* name() const override { return "SDF"; }
};
