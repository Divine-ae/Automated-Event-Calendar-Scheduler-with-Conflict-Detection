#include <iostream>
#include "ConflictDetector.h"
#include "TaskScheduler.h"

int main() {
    std::cout << "=== ChronoSync demo ===\n";
    ConsoleNotifier console;
    NotificationService notifier;
    notifier.subscribe(&console);

    UserCalendar alice("Alice"), bob("Bob");
    alice.addEvent(Event(1, "Lecture",   "Alice", TimeSlot(9 * 60, 10 * 60), 2));
    alice.addEvent(Event(2, "Study",     "Alice", TimeSlot(10 * 60, 12 * 60), 1));
    bob.addEvent  (Event(3, "Lab",       "Bob",   TimeSlot(9 * 60, 11 * 60), 2));

    std::cout << "\n-- Conflict detection --\n";
    TimeSlot proposed(9 * 60 + 30, 10 * 60 + 30);
    std::cout << "Alice free at " << proposed.toString() << "? "
              << (alice.isFree(proposed) ? "yes" : "no") << "\n";

    std::cout << "\n-- Common free slot (60 min) --\n";
    auto common = ConflictDetector::findCommonFreeSlot({&alice, &bob}, 60, 8 * 60, 18 * 60);
    if (common) std::cout << "Alice + Bob free: " << common->toString() << "\n";

    std::cout << "\n-- Task scheduling (EDF) --\n";
    TaskScheduler sched(notifier, std::make_unique<EarliestDeadlineFirst>());
    sched.addTask({10, "Assignment", "Alice", 90, 17 * 60, 2});
    sched.addTask({11, "Gym",        "Alice", 60, 19 * 60, 1});
    sched.scheduleAll(alice);
    for (const Event& e : alice.events())
        std::cout << e.slot().toString() << "  " << e.title() << " (p" << e.priority() << ")\n";

    std::cout << "\n-- Higher-priority meeting bumps a task --\n";
    sched.insertWithBump(alice, Event(20, "Project meeting", "Alice", TimeSlot(10 * 60, 11 * 60), 5));
    for (const Event& e : alice.events())
        std::cout << e.slot().toString() << "  " << e.title() << " (p" << e.priority() << ")\n";
    return 0;
}
