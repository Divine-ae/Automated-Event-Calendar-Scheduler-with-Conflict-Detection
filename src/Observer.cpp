
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
