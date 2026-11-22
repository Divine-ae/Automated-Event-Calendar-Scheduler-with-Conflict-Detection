#  Automated Event & Calendar Scheduler with Conflict Detection

Data Structures & OOP course project (PBL). C++17 scheduling engine with a built-in web server and browser UI.

## What it does
- Per-user calendars with add / move / remove events
- **O(log N) conflict detection** using a self-balancing (AVL) augmented interval tree
- **Automatic task placement** using a binary-heap priority queue
- **Strategy pattern**: swappable heuristics (Earliest Deadline First / Shortest Duration First)
- **Observer pattern**: users are notified when a higher-priority event bumps theirs
- **Multi-user free slot search** by intersecting each member's free intervals
- **Web app**: the engine ships with an HTTP/JSON server and a browser UI (week calendar, live conflict warnings, task queue, team slot finder, notification feed)
- **Persistence**: all calendars, tasks and notifications are saved to `data/chronosync.json`

## Project structure
```
include/   headers (TimeSlot, Event, Task, IntervalTree, UserCalendar,
           ConflictDetector, SchedulingStrategy, TaskScheduler, Observer,
           CalendarService)
src/       implementations + main.cpp (CLI demo) + server.cpp (web server)
frontend/  browser UI (index.html, style.css, script.js - no build step)
third_party/ cpp-httplib (HTTP server) and nlohmann/json (single headers)
tests/     unit tests (no external framework needed)
benchmark/ interval tree vs naive linear scan
docs/      UML class diagram, architecture notes, report, slides
```

## Build & run
```
make          # build everything
make run      # run the demo
make test     # run unit tests
make bench    # run benchmark
```
or with CMake: `mkdir build && cd build && cmake .. && make`

## OOP design
| Concept | Where |
|---|---|
| Encapsulation | `TimeSlot`, `Event`, `UserCalendar` keep data private, expose validated methods |
| Strategy | `SchedulingStrategy` -> `EarliestDeadlineFirst`, `ShortestDurationFirst` |
| Observer | `Observer` / `NotificationService` / `ConsoleNotifier` |

## Sample benchmark (conflict check, no-overlap worst case)
| N events | Interval tree | Linear scan | Speedup |
|---|---|---|---|
| 1,000 | 0.09 us | 1.7 us | ~19x |
| 10,000 | 0.14 us | 17 us | ~120x |
| 100,000 | 0.65 us | 175 us | ~270x |
(Run `make bench` on your machine for your own numbers.)

## Milestone status
| Milestone | Status |
|---|---|
| M1 Core classes + CRUD + tests | Done (basic) |
| M2 Interval tree + conflict detection + benchmark | Done (basic) |
| M3 Priority queue + Strategy heuristics | Done (basic) |
| M4 Multi-user open-slot search | Done (basic) |
| M5 Observer notifications + UI | Observer done, UI pending |
| M6 Integration, docs, final report | Pending |

## Team
Add your team name, members, roll numbers and mentor here.

## Planned next
- Recurring events and reminders
- Authentication for multi-user deployments
- SQLite storage instead of a JSON file
- More unit tests and edge cases
