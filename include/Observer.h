#pragma once
#include <string>
#include <vector>
#include "TimeSlot.h"

// What changed on a calendar (payload sent to observers).
struct ScheduleChange {
    std::string owner;
    std::string eventTitle;
    TimeSlot oldSlot;
    TimeSlot newSlot;
    bool rescheduled;      // false => event was dropped (no free slot found)
    std::string reason;
};

// Observer interface.
class Observer {
public:
    virtual ~Observer() = default;
    virtual void onScheduleChanged(const ScheduleChange& change) = 0;
};

// Subject: the Notification Service publishes changes to subscribers.
class NotificationService {
public:
    void subscribe(Observer* o);
    void unsubscribe(Observer* o);
    void notify(const ScheduleChange& change) const;

private:
    std::vector<Observer*> observers_;
};

// Simple observer that prints alerts to the console.
class ConsoleNotifier : public Observer {
public:
    void onScheduleChanged(const ScheduleChange& c) override;
};
