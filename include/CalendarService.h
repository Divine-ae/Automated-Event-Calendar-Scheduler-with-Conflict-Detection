#pragma once
#include <ctime>
#include <map>
#include <optional>
#include <string>


// Application layer that ties the engine classes together for the web UI.
//
//  * One UserCalendar (and therefore one interval tree) per (user, date).
//  * Every conflict check, bump, free-slot search and auto-placement is
//    delegated to the existing engine classes - nothing is re-implemented here.
//  * Notifications (Observer pattern) are recorded in a log the UI can poll.
//  * State can be persisted to a JSON file and restored on start-up.
//
// Not thread-safe: the HTTP layer serialises access with a mutex.
class CalendarService {
public:
    static constexpr int kDayStart = 8 * 60;    // working window used by the scheduler
    static constexpr int kDayEnd   = 20 * 60;
    static constexpr int kMinutesPerDay = 24 * 60;

    // ---- result / view types ------------------------------------------------
    struct DatedEvent {
        Event event;
        std::string date;  // "YYYY-MM-DD"
    };

    struct Result {
        bool ok = false;
        std::string error;                 // human readable reason when !ok
        int id = 0;                        // id of the created/updated event
        std::vector<Event> conflicts;      // events that blocked the request
        std::optional<TimeSlot> suggestion;// next free slot of the same length
        bool bumped = false;               // true if lower-priority events were moved
    };

    struct ConflictReport {
        std::vector<Event> conflicts;
        std::optional<TimeSlot> suggestion;
        bool canBump = false;              // new priority beats every conflicting event
    };

    struct Notice {                        // one entry of the notification log
        std::string owner, title, date, reason, time;
        TimeSlot oldSlot, newSlot;
        bool rescheduled;
    };

    struct PendingTask {
        Task task;
        std::string date;
    };

    struct Placement {
        Task task;
        std::string date;
        TimeSlot slot;
    };

    struct ScheduleOutcome {
        std::vector<Placement> placed;
        std::vector<PendingTask> unplaced;
        std::string strategy;
    };

    struct Stats {
        int events = 0;
        int users = 0;
        int pendingTasks = 0;
        int conflictsBlocked = 0;
        int bumps = 0;
    };

    // ---- lifecycle ---------------------------------------------------------
    // 'storagePath' empty => in-memory only.
    explicit CalendarService(std::string storagePath = "");
    ~CalendarService() = default;
    CalendarService(const CalendarService&) = delete;
    CalendarService& operator=(const CalendarService&) = delete;

    // ---- users -------------------------------------------------------------
    bool addUser(const std::string& name);
    bool hasUser(const std::string& name) const;
    bool removeUser(const std::string& name);
    std::vector<std::string> users() const;

    // ---- events ------------------------------------------------------------
    Result addEvent(const std::string& user, const std::string& date, const std::string& title,
                    const TimeSlot& slot, int priority, bool bump = false);
    Result updateEvent(int id, const std::string& title, const std::string& date,
                       const TimeSlot& slot, int priority, bool bump = false);
    bool removeEvent(int id);
    std::optional<DatedEvent> getEvent(int id) const;
    // Events of one user, sorted by (date, start). Empty 'from'/'to' = unbounded.
    std::vector<DatedEvent> events(const std::string& user, const std::string& from = "",
                                   const std::string& to = "") const;

    // ---- conflict detection / free time -----------------------------------
    // 'ignoreId' lets the UI check an edit without the event clashing with itself.
    ConflictReport check(const std::string& user, const std::string& date, const TimeSlot& slot,
                         int priority, int ignoreId = 0) const;
    std::vector<TimeSlot> freeSlots(const std::string& user, const std::string& date) const;
    std::vector<TimeSlot> commonSlots(const std::vector<std::string>& users, const std::string& date,
                                      int duration, int maxResults = 6) const;
    // Creates the same event on every listed calendar, all-or-nothing.
    // On failure 'perUserConflicts' says who is busy.
    Result addGroupEvent(const std::vector<std::string>& users, const std::string& date,
                         const std::string& title, const TimeSlot& slot, int priority,
                         std::map<std::string, std::vector<Event>>* perUserConflicts = nullptr);

    // ---- task queue (priority queue + strategy) ---------------------------
    int addTask(const std::string& user, const std::string& date, const std::string& title,
                int duration, int deadline, int priority);
    bool removeTask(int id);
    // Sorted most-urgent-first according to the strategy ("EDF" or "SDF").
    std::vector<PendingTask> tasks(const std::string& user, const std::string& strategy) const;
    ScheduleOutcome autoSchedule(const std::string& user, const std::string& strategy);

    // ---- notifications / stats --------------------------------------------
    std::vector<Notice> notifications(std::size_t limit = 50) const;  // newest first
    void clearNotifications();
    Stats stats() const;

    // ---- misc --------------------------------------------------------------
    void seedDemo(const std::vector<std::string>& weekdays);  // 5 consecutive dates
    static bool validDate(const std::string& d);
    static std::unique_ptr<SchedulingStrategy> makeStrategy(const std::string& name);

private:
    struct Loc { std::string user, date; };

    // Observer that appends to the service's log.
    class LogObserver : public Observer {
    public:
        explicit LogObserver(CalendarService& s) : svc_(s) {}
        void onScheduleChanged(const ScheduleChange& c) override;
    private:
        CalendarService& svc_;
    };

    UserCalendar& calendar(const std::string& user, const std::string& date);
    const UserCalendar* findCalendar(const std::string& user, const std::string& date) const;
    Result place(const Event& e, const std::string& date, bool bump);
    void load();
    void persist() const;
    int newId() { return nextId_++; }

    std::string path_;
    std::map<std::string, std::map<std::string, UserCalendar>> calendars_;  // user -> date -> calendar
    std::map<int, Loc> where_;                                              // event id -> location
    std::map<int, PendingTask> tasks_;
    std::vector<Notice> log_;
    NotificationService notifier_;
    LogObserver logObserver_;
    std::string ctxDate_;  // date being processed while a notification is raised
    int nextId_ = 1;
    int conflictsBlocked_ = 0;
    int bumps_ = 0;
    bool loading_ = false;
};
