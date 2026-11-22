#include "Observer.h"
#include <algorithm>
#include <iostream>

void NotificationService::subscribe(Observer* o) { observers_.push_back(o); }

void NotificationService::unsubscribe(Observer* o) {
    observers_.erase(std::remove(observers_.begin(), observers_.end(), o), observers_.end());
}

void NotificationService::notify(const ScheduleChange& c) const {
    for (Observer* o : observers_) o->onScheduleChanged(c);
}

void ConsoleNotifier::onScheduleChanged(const ScheduleChange& c) {
    std::cout << "[ALERT] " << c.owner << ": \"" << c.eventTitle << "\" ";
    if (c.rescheduled)
        std::cout << "moved " << c.oldSlot.toString() << " -> " << c.newSlot.toString();
    else
        std::cout << "(" << c.oldSlot.toString() << ") could not be rescheduled";
    std::cout << " (" << c.reason << ")\n";
}
