#pragma once
#include <string>
#include <vector>
#include "TimeSlot.h"

// What changed on a calendar (payload sent to observers).

// Observer interface.

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
