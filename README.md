#  Automated Event & Calendar Scheduler with Conflict Detection

Data Structures & OOP course project (PBL). 

## What it does
- Per-user calendars with add / move / remove events
- **O(log N) conflict detection** using a self-balancing (AVL) augmented interval tree
- **Automatic task placement** using a binary-heap priority queue
- **Strategy pattern**: swappable heuristics (Earliest Deadline First / Shortest Duration First)
- **Observer pattern**: users are notified when a higher-priority event bumps theirs
- **Multi-user free slot search** by intersecting each member's free intervals

## Project structure
```
include/   headers (TimeSlot, Event, Task, IntervalTree, UserCalendar,
           ConflictDetector, SchedulingStrategy, TaskScheduler, Observer)
src/       implementations + main.cpp (CLI demo)
tests/     unit tests (no external framework needed)
docs/      UML class diagram, architecture notes, report, slides
```



## OOP design
| Concept | Where |
|---|---|
| Encapsulation | `TimeSlot`, `Event`, `UserCalendar` keep data private, expose validated methods |
| Strategy | `SchedulingStrategy` -> `EarliestDeadlineFirst`, `ShortestDurationFirst` |
| Observer | `Observer` / `NotificationService` / `ConsoleNotifier` |



## Milestone status
| Milestone | Status |
|---|---|
| M1 Core classes + CRUD + tests | Done (basic) |
| M2 Interval tree + conflict detection | Done (basic) |
| M3 Priority queue + Strategy heuristics | Done (basic) |
| M4 Multi-user open-slot search | Done (basic) |
| M5 Observer notifications + UI | Observer done, UI pending |
| M6 Integration, docs, final report | Pending |




