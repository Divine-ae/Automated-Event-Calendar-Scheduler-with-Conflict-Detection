#pragma once
#include <memory>
#include <vector>
#include "Observer.h"
#include "SchedulingStrategy.h"
#include "Task.h"
#include "UserCalendar.h"

// Scheduling Engine: binary-heap priority queue of pending tasks + swappable strategy.
class TaskScheduler {
public:
    TaskScheduler(NotificationService& notifier, std::unique_ptr<SchedulingStrategy> strategy,
                  int dayStart = 8 * 60, int dayEnd = 20 * 60);

    void addTask(const Task& t);                         // O(log N)
    void setStrategy(std::unique_ptr<SchedulingStrategy> s); // re-heapifies
    std::size_t pending() const { return heap_.size(); }
    const char* strategyName() const { return strategy_->name(); }

    // Place every pending task into the calendar. Returns tasks that did not fit.
    std::vector<Task> scheduleAll(UserCalendar& cal);

    // Insert a high-priority event; lower-priority conflicting events get bumped,
    // re-placed in the next free slot, and observers are notified.
    bool insertWithBump(UserCalendar& cal, const Event& e);

private:
    bool lessUrgent(const Task& a, const Task& b) const;
    Task popNext();

    NotificationService& notifier_;
    std::unique_ptr<SchedulingStrategy> strategy_;
    std::vector<Task> heap_;
    int dayStart_, dayEnd_;
};
