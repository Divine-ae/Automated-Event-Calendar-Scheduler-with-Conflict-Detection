#include <cstdio>
#include <memory>
#include <random>
#include <stdexcept>
#include "ConflictDetector.h"
#include "TaskScheduler.h"


}

void testIntervalTree() {
    IntervalTree t;
    t.insert(TimeSlot(10, 20), 1);
    t.insert(TimeSlot(30, 40), 2);
    t.insert(TimeSlot(50, 60), 3);
    CHECK(t.anyOverlap(TimeSlot(15, 35)));
    CHECK(!t.anyOverlap(TimeSlot(20, 30)));
    CHECK(t.findOverlaps(TimeSlot(15, 55)).size() == 3);
    CHECK(t.remove(TimeSlot(30, 40), 2));
    CHECK(!t.anyOverlap(TimeSlot(25, 45)));
    CHECK(t.size() == 2);

    // Randomised check against brute force + balance check
    IntervalTree big;
    std::vector<std::pair<TimeSlot, int>> ref;
    std::mt19937 rng(42);
    for (int i = 0; i < 2000; ++i) {
        int s = rng() % 100000, len = 1 + rng() % 50;
        big.insert(TimeSlot(s, s + len), i);
        ref.emplace_back(TimeSlot(s, s + len), i);
    }
    for (int i = 0; i < 500; ++i) {
        big.remove(ref[i].first, ref[i].second);
    }
    ref.erase(ref.begin(), ref.begin() + 500);
    bool allMatch = true;
    for (int q = 0; q < 500; ++q) {
        int s = rng() % 100000;
        TimeSlot query(s, s + 1 + rng() % 200);
        bool brute = false;
        for (auto& r : ref) if (r.first.overlaps(query)) { brute = true; break; }
        if (brute != big.anyOverlap(query)) allMatch = false;
    }
    CHECK(allMatch);
    CHECK(big.height() <= 20);      // AVL: ~1.44*log2(1500) ~ 16
}

void testCalendar() {
    UserCalendar cal("Test");
    CHECK(cal.addEvent(Event(1, "A", "Test", TimeSlot(60, 120))));
    CHECK(!cal.addEvent(Event(2, "B", "Test", TimeSlot(100, 160))));  // conflict
    CHECK(cal.addEvent(Event(3, "C", "Test", TimeSlot(120, 180))));
    CHECK(cal.moveEvent(3, TimeSlot(200, 260)));
    CHECK(!cal.moveEvent(3, TimeSlot(90, 130)));                      // conflict, rolled back
    CHECK(cal.getEvent(3)->slot() == TimeSlot(200, 260));
    CHECK(cal.removeEvent(1));
    CHECK(cal.isFree(TimeSlot(60, 120)));
    auto free = cal.freeSlots(0, 300);
    CHECK(free.size() == 2);        // [0,200) and [260,300)
}

void testCommonFreeSlot() {
    UserCalendar a("A"), b("B");
    a.addEvent(Event(1, "x", "A", TimeSlot(480, 600)));
    b.addEvent(Event(2, "y", "B", TimeSlot(540, 660)));
    auto s = ConflictDetector::findCommonFreeSlot({&a, &b}, 30, 480, 720);
    CHECK(s.has_value() && s->start() == 660);
}

void testScheduler() {
    NotificationService n;
    TaskScheduler sched(n, std::make_unique<EarliestDeadlineFirst>(), 480, 1200);
    UserCalendar cal("U");
    sched.addTask({1, "late",  "U", 60, 1000, 1});
    sched.addTask({2, "early", "U", 60, 600, 1});
    CHECK(sched.pending() == 2);
    auto left = sched.scheduleAll(cal);
    CHECK(left.empty());
    CHECK(cal.getEvent(2)->slot().start() == 480);   // EDF puts earliest deadline first

    // Bumping
    CHECK(sched.insertWithBump(cal, Event(9, "Urgent", "U", TimeSlot(480, 540), 5)));
    CHECK(cal.getEvent(9) != nullptr);
    CHECK(cal.getEvent(2)->slot().start() >= 540);   // moved
    CHECK(!sched.insertWithBump(cal, Event(10, "Weak", "U", TimeSlot(480, 540), 1)));
}

int main() {
    testTimeSlot();
    testIntervalTree();
    testCalendar();
    testCommonFreeSlot();
    testScheduler();
    std::printf("%d/%d checks passed\n", total - failed, total);
    return failed ? 1 : 0;
}
