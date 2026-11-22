
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// First free slot of 'duration' minutes at/after 'from', skipping one event id.
// Uses the engine's firstFreeSlot unless an event has to be ignored.
std::optional<TimeSlot> freeAfter(const UserCalendar& cal, int ignoreId, int duration, int from) {
    from = std::max(0, from);
    const int to = CalendarService::kMinutesPerDay;
    if (ignoreId == 0 || !cal.getEvent(ignoreId)) return cal.firstFreeSlot(duration, from, to);
    int cursor = from;
    for (const Event& e : cal.events()) {
        if (e.id() == ignoreId || e.slot().end() <= cursor) continue;
        if (e.slot().start() - cursor >= duration) break;
        cursor = std::max(cursor, e.slot().end());
    }
    if (cursor + duration <= to) return TimeSlot(cursor, cursor + duration);
    return std::nullopt;
}

std::optional<TimeSlot> suggestSlot(const UserCalendar& cal, int ignoreId, const TimeSlot& want) {
    if (auto s = freeAfter(cal, ignoreId, want.duration(), want.start())) return s;
    return freeAfter(cal, ignoreId, want.duration(), 0);
}

}  // namespace

// ---------------------------------------------------------------------------
// Observer: turn engine notifications into log entries
// ---------------------------------------------------------------------------
void CalendarService::LogObserver::onScheduleChanged(const ScheduleChange& c) {
    svc_.log_.push_back({c.owner, c.eventTitle, svc_.ctxDate_, c.reason, nowIso(),
                         c.oldSlot, c.newSlot, c.rescheduled});
    if (svc_.log_.size() > 200) svc_.log_.erase(svc_.log_.begin(), svc_.log_.begin() + 50);
}

// ---------------------------------------------------------------------------
// Lifecycle & persistence
// ---------------------------------------------------------------------------
CalendarService::CalendarService(std::string storagePath)
    : path_(std::move(storagePath)), logObserver_(*this) {
    notifier_.subscribe(&logObserver_);
    if (!path_.empty()) load();
}

void CalendarService::persist() const {
    if (path_.empty() || loading_) return;
    json j;
    j["nextId"] = nextId_;
    j["conflictsBlocked"] = conflictsBlocked_;
    j["bumps"] = bumps_;
    j["users"] = users();
    j["events"] = json::array();
    for (const auto& [user, days] : calendars_)
        for (const auto& [date, cal] : days)
            for (const Event& e : cal.events())
                j["events"].push_back({{"id", e.id()}, {"user", user}, {"date", date},
                                       {"title", e.title()}, {"start", e.slot().start()},
                                       {"end", e.slot().end()}, {"priority", e.priority()}});
    j["tasks"] = json::array();
    for (const auto& [id, pt] : tasks_)
        j["tasks"].push_back({{"id", id}, {"user", pt.task.owner}, {"date", pt.date},
                              {"title", pt.task.title}, {"duration", pt.task.duration},
                              {"deadline", pt.task.deadline}, {"priority", pt.task.priority}});
    j["log"] = json::array();
    for (const Notice& n : log_)
        j["log"].push_back({{"owner", n.owner}, {"title", n.title}, {"date", n.date},
                            {"reason", n.reason}, {"time", n.time},
                            {"oldStart", n.oldSlot.start()}, {"oldEnd", n.oldSlot.end()},
                            {"newStart", n.newSlot.start()}, {"newEnd", n.newSlot.end()},
                            {"rescheduled", n.rescheduled}});

    // Write to a temp file first so a crash never leaves a half-written state file.
    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return;
        out << j.dump(1);
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path_, ec);
    if (ec) std::remove(tmp.c_str());
}

void CalendarService::load() {
    std::ifstream in(path_);
    if (!in) return;  // first run
    loading_ = true;
    try {
        json j = json::parse(in);
        nextId_ = j.value("nextId", 1);
        conflictsBlocked_ = j.value("conflictsBlocked", 0);
        bumps_ = j.value("bumps", 0);
        for (const auto& u : j.value("users", json::array())) addUser(u.get<std::string>());
        for (const auto& e : j.value("events", json::array())) {
            try {
                const std::string user = e.at("user"), date = e.at("date");
                Event ev(e.at("id"), e.at("title"), user, TimeSlot(e.at("start"), e.at("end")),
                         e.value("priority", 1));
                addUser(user);
                if (calendar(user, date).addEvent(ev)) where_[ev.id()] = {user, date};
                nextId_ = std::max(nextId_, ev.id() + 1);
            } catch (const std::exception&) { /* skip a malformed record */ }
        }
        for (const auto& t : j.value("tasks", json::array())) {
            try {
                Task task{t.at("id"), t.at("title"), t.at("user"), t.at("duration"),
                          t.at("deadline"), t.value("priority", 1)};
                tasks_.emplace(task.id, PendingTask{task, t.at("date")});
                nextId_ = std::max(nextId_, task.id + 1);
            } catch (const std::exception&) {}
        }
        for (const auto& n : j.value("log", json::array())) {
            try {
                log_.push_back({n.at("owner"), n.at("title"), n.value("date", ""),
                                n.value("reason", ""), n.value("time", ""),
                                TimeSlot(n.at("oldStart"), n.at("oldEnd")),
                                TimeSlot(n.at("newStart"), n.at("newEnd")),
                                n.value("rescheduled", true)});
            } catch (const std::exception&) {}
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "warning: could not read %s (%s); starting empty\n", path_.c_str(), ex.what());
        calendars_.clear(); where_.clear(); tasks_.clear(); log_.clear();
        in.close();
        std::error_code ec;
        std::filesystem::rename(path_, path_ + ".bad", ec);
    }
    loading_ = false;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
bool CalendarService::validDate(const std::string& d) {
    if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9})
        if (d[i] < '0' || d[i] > '9') return false;
    int m = std::stoi(d.substr(5, 2)), day = std::stoi(d.substr(8, 2));
    return m >= 1 && m <= 12 && day >= 1 && day <= 31;
}

std::unique_ptr<SchedulingStrategy> CalendarService::makeStrategy(const std::string& name) {
    if (name == "SDF") return std::make_unique<ShortestDurationFirst>();
    return std::make_unique<EarliestDeadlineFirst>();
}

UserCalendar& CalendarService::calendar(const std::string& user, const std::string& date) {
    auto& days = calendars_[user];
    auto it = days.find(date);
    if (it == days.end()) it = days.emplace(date, UserCalendar(user)).first;
    return it->second;
}

const UserCalendar* CalendarService::findCalendar(const std::string& user, const std::string& date) const {
    auto u = calendars_.find(user);
    if (u == calendars_.end()) return nullptr;
    auto d = u->second.find(date);
    return d == u->second.end() ? nullptr : &d->second;
}

// ---------------------------------------------------------------------------
// Users
// ---------------------------------------------------------------------------
bool CalendarService::addUser(const std::string& name) {
    if (name.empty() || calendars_.count(name)) return false;
    calendars_[name];
    persist();
    return true;
}

bool CalendarService::hasUser(const std::string& name) const { return calendars_.count(name) > 0; }

bool CalendarService::removeUser(const std::string& name) {
    if (!calendars_.erase(name)) return false;
    for (auto it = where_.begin(); it != where_.end();)
        it = (it->second.user == name) ? where_.erase(it) : std::next(it);
    for (auto it = tasks_.begin(); it != tasks_.end();)
        it = (it->second.task.owner == name) ? tasks_.erase(it) : std::next(it);
    persist();
    return true;
}

std::vector<std::string> CalendarService::users() const {
    std::vector<std::string> v;
    for (const auto& kv : calendars_) v.push_back(kv.first);
    return v;
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
CalendarService::Result CalendarService::place(const Event& e, const std::string& date, bool bump) {
    Result r;
    r.id = e.id();
    UserCalendar& cal = calendar(e.owner(), date);

    std::vector<Event> clashes = ConflictDetector::conflicts(cal, e.slot());  // O(k + log N)
    if (clashes.empty()) {
        cal.addEvent(e);
        where_[e.id()] = {e.owner(), date};
        r.ok = true;
        return r;
    }

    r.conflicts = clashes;
    r.suggestion = suggestSlot(cal, 0, e.slot());

    if (!bump) {
        ++conflictsBlocked_;
        r.error = "This time overlaps with " + std::to_string(clashes.size()) + " existing event(s).";
        return r;
    }
    for (const Event& c : clashes)
        if (c.priority() >= e.priority()) {
            ++conflictsBlocked_;
            r.error = "\"" + c.title() + "\" has equal or higher priority, so it cannot be bumped.";
            return r;
        }

    // Priority bump: engine removes lower-priority events, inserts ours and re-places them.
    ctxDate_ = date;
    TaskScheduler engine(notifier_, std::make_unique<EarliestDeadlineFirst>(), kDayStart, kDayEnd);
    if (!engine.insertWithBump(cal, e)) {
        ++conflictsBlocked_;
        r.error = "Could not bump the conflicting events.";
        return r;
    }
    where_[e.id()] = {e.owner(), date};
    for (const Event& c : clashes) {
        if (cal.getEvent(c.id())) continue;
        // No room left today: keep it as a pending task instead of silently losing it.
        where_.erase(c.id());
        tasks_[c.id()] = {Task{c.id(), c.title(), c.owner(), c.slot().duration(), kDayEnd, c.priority()}, date};
    }
    bumps_ += static_cast<int>(clashes.size());
    r.ok = true;
    r.bumped = true;
    return r;
}

CalendarService::Result CalendarService::addEvent(const std::string& user, const std::string& date,
                                                  const std::string& title, const TimeSlot& slot,
                                                  int priority, bool bump) {
    Result r;
    if (!hasUser(user)) { r.error = "Unknown user."; return r; }
    if (!validDate(date)) { r.error = "Invalid date."; return r; }
    if (slot.end() > kMinutesPerDay) { r.error = "Event must end by midnight."; return r; }
    r = place(Event(newId(), title, user, slot, priority), date, bump);
    persist();
    return r;
}

CalendarService::Result CalendarService::updateEvent(int id, const std::string& title,
                                                     const std::string& date, const TimeSlot& slot,
                                                     int priority, bool bump) {
    Result r;
    auto it = where_.find(id);
    if (it == where_.end()) { r.error = "Event not found."; return r; }
    if (!validDate(date)) { r.error = "Invalid date."; return r; }
    if (slot.end() > kMinutesPerDay) { r.error = "Event must end by midnight."; return r; }

    const Loc loc = it->second;
    UserCalendar& oldCal = calendar(loc.user, loc.date);
    Event old = *oldCal.getEvent(id);
    oldCal.removeEvent(id);
    where_.erase(id);

    r = place(Event(id, title, loc.user, slot, priority), date, bump);
    if (!r.ok) {  // roll back to the original event
        calendar(loc.user, loc.date).addEvent(old);
        where_[id] = loc;
    }
    persist();
    return r;
}

bool CalendarService::removeEvent(int id) {
    auto it = where_.find(id);
    if (it == where_.end()) return false;
    calendar(it->second.user, it->second.date).removeEvent(id);
    where_.erase(it);
    persist();
    return true;
}

std::optional<CalendarService::DatedEvent> CalendarService::getEvent(int id) const {
    auto it = where_.find(id);
    if (it == where_.end()) return std::nullopt;
    const UserCalendar* cal = findCalendar(it->second.user, it->second.date);
    const Event* e = cal ? cal->getEvent(id) : nullptr;
    if (!e) return std::nullopt;
    return DatedEvent{*e, it->second.date};
}

std::vector<CalendarService::DatedEvent> CalendarService::events(const std::string& user,
                                                                 const std::string& from,
                                                                 const std::string& to) const {
    std::vector<DatedEvent> out;
    auto u = calendars_.find(user);
    if (u == calendars_.end()) return out;
    for (const auto& [date, cal] : u->second) {
        if (!from.empty() && date < from) continue;
        if (!to.empty() && date > to) continue;
        for (const Event& e : cal.events()) out.push_back({e, date});
    }
    return out;  // std::map iterates dates in order and cal.events() is sorted by start
}

// ---------------------------------------------------------------------------
// Conflict detection / free time
// ---------------------------------------------------------------------------
CalendarService::ConflictReport CalendarService::check(const std::string& user, const std::string& date,
                                                       const TimeSlot& slot, int priority,
                                                       int ignoreId) const {
    ConflictReport rep;
    const UserCalendar* cal = findCalendar(user, date);
    if (!cal) return rep;
    for (const Event& e : ConflictDetector::conflicts(*cal, slot))
        if (e.id() != ignoreId) rep.conflicts.push_back(e);
    if (rep.conflicts.empty()) return rep;
    rep.suggestion = suggestSlot(*cal, ignoreId, slot);
    rep.canBump = std::all_of(rep.conflicts.begin(), rep.conflicts.end(),
                              [&](const Event& c) { return c.priority() < priority; });
    return rep;
}

std::vector<TimeSlot> CalendarService::freeSlots(const std::string& user, const std::string& date) const {
    const UserCalendar* cal = findCalendar(user, date);
    if (!cal) return {TimeSlot(kDayStart, kDayEnd)};
    return cal->freeSlots(kDayStart, kDayEnd);
}

std::vector<TimeSlot> CalendarService::commonSlots(const std::vector<std::string>& names,
                                                   const std::string& date, int duration,
                                                   int maxResults) const {
    std::vector<TimeSlot> out;
    if (names.empty() || duration <= 0) return out;
    std::vector<std::unique_ptr<UserCalendar>> empties;  // stand-ins for users with a free day
    std::vector<const UserCalendar*> cals;
    for (const std::string& n : names) {
        const UserCalendar* c = findCalendar(n, date);
        if (!c) { empties.push_back(std::make_unique<UserCalendar>(n)); c = empties.back().get(); }
        cals.push_back(c);
    }
    int from = kDayStart;
    while (static_cast<int>(out.size()) < maxResults && from < kDayEnd) {
        auto s = ConflictDetector::findCommonFreeSlot(cals, duration, from, kDayEnd);
        if (!s) break;
        out.push_back(*s);
        from = s->end();
    }
    return out;
}

CalendarService::Result CalendarService::addGroupEvent(
    const std::vector<std::string>& names, const std::string& date, const std::string& title,
    const TimeSlot& slot, int priority, std::map<std::string, std::vector<Event>>* perUser) {
    Result r;
    if (names.empty()) { r.error = "Pick at least one person."; return r; }
    if (!validDate(date)) { r.error = "Invalid date."; return r; }
    if (slot.end() > kMinutesPerDay) { r.error = "Event must end by midnight."; return r; }
    for (const std::string& n : names)
        if (!hasUser(n)) { r.error = "Unknown user: " + n; return r; }

    bool clash = false;
    for (const std::string& n : names) {
        const UserCalendar* c = findCalendar(n, date);
        if (!c) continue;
        auto hits = ConflictDetector::conflicts(*c, slot);
        if (!hits.empty()) {
            clash = true;
            if (perUser) (*perUser)[n] = hits;
        }
    }
    if (clash) {
        ++conflictsBlocked_;
        r.error = "Not everyone is free at that time.";
        persist();
        return r;
    }
    for (const std::string& n : names) {
        Event e(newId(), title, n, slot, priority);
        calendar(n, date).addEvent(e);
        where_[e.id()] = {n, date};
        if (!r.id) r.id = e.id();
    }
    r.ok = true;
    persist();
    return r;
}

// ---------------------------------------------------------------------------
// Task queue
// ---------------------------------------------------------------------------
int CalendarService::addTask(const std::string& user, const std::string& date, const std::string& title,
                             int duration, int deadline, int priority) {
    int id = newId();
    tasks_[id] = {Task{id, title, user, duration, deadline, priority}, date};
    persist();
    return id;
}

bool CalendarService::removeTask(int id) {
    bool ok = tasks_.erase(id) > 0;
    if (ok) persist();
    return ok;
}

std::vector<CalendarService::PendingTask> CalendarService::tasks(const std::string& user,
                                                                 const std::string& strategy) const {
    std::vector<PendingTask> v;
    for (const auto& kv : tasks_)
        if (user.empty() || kv.second.task.owner == user) v.push_back(kv.second);
    auto strat = makeStrategy(strategy);
    std::sort(v.begin(), v.end(), [&](const PendingTask& a, const PendingTask& b) {
        return strat->higherPriority(a.task, b.task);
    });
    return v;
}

CalendarService::ScheduleOutcome CalendarService::autoSchedule(const std::string& user,
                                                               const std::string& strategy) {
    ScheduleOutcome out;
    out.strategy = makeStrategy(strategy)->name();

    std::map<std::string, std::vector<Task>> byDate;  // each day is scheduled on its own calendar
    for (const auto& kv : tasks_)
        if (kv.second.task.owner == user) byDate[kv.second.date].push_back(kv.second.task);

    for (auto& [date, list] : byDate) {
        ctxDate_ = date;
        TaskScheduler engine(notifier_, makeStrategy(strategy), kDayStart, kDayEnd);
        for (const Task& t : list) engine.addTask(t);            // binary-heap push, O(log N)
        UserCalendar& cal = calendar(user, date);
        std::vector<Task> left = engine.scheduleAll(cal);        // pops in strategy order

        std::set<int> leftIds;
        for (const Task& t : left) leftIds.insert(t.id);
        for (const Task& t : list) {
            if (leftIds.count(t.id)) { out.unplaced.push_back({t, date}); continue; }
            const Event* e = cal.getEvent(t.id);
            if (!e) continue;
            where_[t.id] = {user, date};
            out.placed.push_back({t, date, e->slot()});
            tasks_.erase(t.id);
        }
    }
    std::sort(out.placed.begin(), out.placed.end(), [](const Placement& a, const Placement& b) {
        return a.date != b.date ? a.date < b.date : a.slot.start() < b.slot.start();
    });
    persist();
    return out;
}

// ---------------------------------------------------------------------------
// Notifications / stats
// ---------------------------------------------------------------------------
std::vector<CalendarService::Notice> CalendarService::notifications(std::size_t limit) const {
    std::vector<Notice> v;
    for (auto it = log_.rbegin(); it != log_.rend() && v.size() < limit; ++it) v.push_back(*it);
    return v;
}

void CalendarService::clearNotifications() {
    log_.clear();
    persist();
}

CalendarService::Stats CalendarService::stats() const {
    Stats s;
    s.events = static_cast<int>(where_.size());
    s.users = static_cast<int>(calendars_.size());
    s.pendingTasks = static_cast<int>(tasks_.size());
    s.conflictsBlocked = conflictsBlocked_;
    s.bumps = bumps_;
    return s;
}

// ---------------------------------------------------------------------------
// Demo data
// ---------------------------------------------------------------------------
void CalendarService::seedDemo(const std::vector<std::string>& d) {
    if (d.size() < 5) return;
    for (const auto& date : d) if (!validDate(date)) return;
    addUser("Alice");
    addUser("Bob");
    auto add = [&](const char* who, int day, const char* title, int h1, int m1, int h2, int m2, int p) {
        addEvent(who, d[day], title, TimeSlot(h1 * 60 + m1, h2 * 60 + m2), p);
    };
    add("Alice", 0, "Algorithms Lecture", 9, 0, 10, 30, 3);
    add("Alice", 0, "Study Group", 11, 0, 12, 30, 2);
    add("Alice", 0, "Gym", 17, 0, 18, 0, 1);
    add("Alice", 1, "Project Stand-up", 9, 30, 10, 0, 3);
    add("Alice", 1, "Lab Session", 10, 30, 12, 30, 2);
    add("Alice", 1, "Coding", 14, 0, 16, 0, 2);
    add("Alice", 2, "Database Lecture", 9, 0, 10, 30, 3);
    add("Alice", 2, "Mentor Meeting", 13, 0, 14, 0, 4);
    add("Alice", 3, "Project Work", 10, 0, 12, 0, 2);
    add("Alice", 3, "Code Review", 15, 0, 16, 0, 3);
    add("Alice", 4, "Weekly Review", 10, 0, 11, 0, 2);
    add("Alice", 4, "Demo Prep", 14, 0, 15, 30, 4);
    add("Bob", 0, "Lab", 9, 0, 11, 0, 2);
    add("Bob", 0, "Project Sync", 14, 0, 15, 0, 3);
    add("Bob", 1, "Lecture", 10, 0, 11, 30, 3);
    add("Bob", 1, "Part-time Job", 15, 0, 18, 0, 2);
    add("Bob", 2, "Study", 9, 0, 10, 0, 1);
    add("Bob", 2, "Mentor Meeting", 13, 0, 14, 0, 4);
    add("Bob", 3, "Workshop", 11, 0, 13, 0, 3);
    add("Bob", 4, "Weekly Review", 10, 0, 11, 0, 2);
    addTask("Alice", d[0], "Assignment 3", 90, 17 * 60, 3);
    addTask("Alice", d[0], "Read chapter 7", 45, 20 * 60, 1);
    addTask("Alice", d[1], "Email advisor", 20, 12 * 60, 2);
    addTask("Alice", d[2], "Prepare slides", 120, 18 * 60, 3);
}
