
#pragma once

#include <ctime>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ConflictDetector.h"
#include "Observer.h"
#include "Task.h"
#include "TaskScheduler.h"
#include "UserCalendar.h"

// Application layer connecting engine classes to the web UI.
class CalendarService {
public:
    static constexpr int kDayStart = 8 * 60;
    static constexpr int kDayEnd = 20 * 60;
    static constexpr int kMinutesPerDay = 24 * 60;

    struct DatedEvent {
        Event event;
        std::string date;
    };

    struct Result {
        bool ok = false;
        std::string error;
        int id = 0;
        std::vector<Event> conflicts;
        std::optional<TimeSlot> suggestion;
        bool bumped = false;
    };

    struct ConflictReport {
        std::vector<Event> conflicts;
        std::optional<TimeSlot> suggestion;
        bool canBump = false;
    };

    struct Notice {
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

    explicit CalendarService(std::string storagePath = "");
    ~CalendarService() = default;

    CalendarService(const CalendarService&) = delete;
    CalendarService& operator=(const CalendarService&) = delete;

    bool addUser(const std::string& name);
    bool hasUser(const std::string& name) const;
    bool removeUser(const std::string& name);
    std::vector<std::string> users() const;

    Result addEvent(const std::string& user, const std::string& date,
                    const std::string& title, const TimeSlot& slot,
                    int priority, bool bump = false);

    Result updateEvent(int id, const std::string& title,
                       const std::string& date, const TimeSlot& slot,
                       int priority, bool bump = false);

    bool removeEvent(int id);
    std::optional<DatedEvent> getEvent(int id) const;

    std::vector<DatedEvent> events(
        const std::string& user,
        const std::string& from = "",
        const std::string& to = "") const;

    ConflictReport check(const std::string& user, const std::string& date,
                         const TimeSlot& slot, int priority,
                         int ignoreId = 0) const;

    std::vector<TimeSlot> freeSlots(
        const std::string& user, const std::string& date) const;

    std::vector<TimeSlot> commonSlots(
        const std::vector<std::string>& users, const std::string& date,
        int duration, int maxResults = 6) const;

    Result addGroupEvent(
        const std::vector<std::string>& users, const std::string& date,
        const std::string& title, const TimeSlot& slot, int priority,
        std::map<std::string, std::vector<Event>>* perUserConflicts = nullptr);

    int addTask(const std::string& user, const std::string& date,
                const std::string& title, int duration,
                int deadline, int priority);

    bool removeTask(int id);

    std::vector<PendingTask> tasks(
        const std::string& user, const std::string& strategy) const;

    ScheduleOutcome autoSchedule(
        const std::string& user, const std::string& strategy);

    std::vector<Notice> notifications(std::size_t limit = 50) const;
    void clearNotifications();
    Stats stats() const;

    void seedDemo(const std::vector<std::string>& weekdays);
    static bool validDate(const std::string& d);
    static std::unique_ptr<SchedulingStrategy> makeStrategy(
        const std::string& name);

private:
    struct Loc {
        std::string user, date;
    };

    class LogObserver : public Observer {
    public:
        explicit LogObserver(CalendarService& s) : svc_(s) {}
        void onScheduleChanged(const ScheduleChange& c) override;

    private:
        CalendarService& svc_;
    };

    UserCalendar& calendar(const std::string& user, const std::string& date);

    const UserCalendar* findCalendar(
        const std::string& user, const std::string& date) const;

    Result place(const Event& e, const std::string& date, bool bump);
    void load();
    void persist() const;

    int newId() {
        return nextId_++;
    }

    std::string path_;

    std::map<std::string, std::map<std::string, UserCalendar>> calendars_;
    std::map<int, Loc> where_;
    std::map<int, PendingTask> tasks_;

    std::vector<Notice> log_;

    NotificationService notifier_;
    LogObserver logObserver_;

    std::string ctxDate_;

    int nextId_ = 1;
    int conflictsBlocked_ = 0;
    int bumps_ = 0;

    bool loading_ = false;
};
