#include <iostream>
#include "ConflictDetector.h"
#include "TaskScheduler.h"

int main() {
    std::cout << "=== ChronoSync demo ===\n";
    ConsoleNotifier console;
    NotificationService notifier;
    notifier.subscribe(&console);

    UserCalendar alice("Alice"), bob("Bob");

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
