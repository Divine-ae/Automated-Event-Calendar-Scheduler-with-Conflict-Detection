#include "TaskScheduler.h"
#include <algorithm>

TaskScheduler::TaskScheduler(NotificationService& n, std::unique_ptr<SchedulingStrategy> s,
                             int dayStart, int dayEnd)
    : notifier_(n), strategy_(std::move(s)), dayStart_(dayStart), dayEnd_(dayEnd) {}

bool TaskScheduler::lessUrgent(const Task& a, const Task& b) const {
    return strategy_->higherPriority(b, a);
}

void TaskScheduler::addTask(const Task& t) {
    heap_.push_back(t);
    std::push_heap(heap_.begin(), heap_.end(),
                   [this](const Task& a, const Task& b) { return lessUrgent(a, b); });
}

void TaskScheduler::setStrategy(std::unique_ptr<SchedulingStrategy> s) {
    strategy_ = std::move(s);
    std::make_heap(heap_.begin(), heap_.end(),
                   [this](const Task& a, const Task& b) { return lessUrgent(a, b); });
}

Task TaskScheduler::popNext() {
    std::pop_heap(heap_.begin(), heap_.end(),
                  [this](const Task& a, const Task& b) { return lessUrgent(a, b); });
    Task t = heap_.back();
    heap_.pop_back();
    return t;
}

std::vector<Task> TaskScheduler::scheduleAll(UserCalendar& cal) {
    std::vector<Task> unplaced;
    while (!heap_.empty()) {
        Task t = popNext();
        int limit = std::min(t.deadline, dayEnd_);
        auto slot = cal.firstFreeSlot(t.duration, dayStart_, limit);
        if (slot) cal.addEvent(Event(t.id, t.title, t.owner, *slot, t.priority));
        else unplaced.push_back(t);
    }
    return unplaced;
}

bool TaskScheduler::insertWithBump(UserCalendar& cal, const Event& e) {
    std::vector<Event> clashes = cal.conflictsWith(e.slot());
    for (const Event& c : clashes)
        if (c.priority() >= e.priority()) return false;  // cannot bump equal/higher priority

    for (const Event& c : clashes) cal.removeEvent(c.id());
    cal.addEvent(e);

    for (const Event& c : clashes) {
        auto slot = cal.firstFreeSlot(c.slot().duration(), dayStart_, dayEnd_);
        if (slot) {
            Event moved(c.id(), c.title(), c.owner(), *slot, c.priority());
            cal.addEvent(moved);
            notifier_.notify({c.owner(), c.title(), c.slot(), *slot, true,
                              "bumped by \"" + e.title() + "\""});
        } else {
            notifier_.notify({c.owner(), c.title(), c.slot(), c.slot(), false,
                              "bumped by \"" + e.title() + "\""});
        }
    }
    return true;
}
