// ChronoSync web server: REST/JSON API over the C++ engine + static frontend.
//
//   ./build/chronosync_server [--port 8080] [--host 127.0.0.1]
//                             [--data FILE] [--frontend frontend]
//
// Open http://localhost:8080 once it is running.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>

#include "CalendarService.h"
#include "httplib.h"
#include "json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct ApiError {
    int status;
    std::string message;
};

// ---- small helpers --------------------------------------------------------
std::string fmtTime(int minutes) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    return buf;
}

int parseTime(const std::string& s) {
    int h = 0, m = 0;
    char extra = 0;
    if (std::sscanf(s.c_str(), "%d:%d%c", &h, &m, &extra) != 2 || h < 0 || h > 24 || m < 0 || m > 59 ||
        (h == 24 && m != 0))
        throw ApiError{400, "Invalid time \"" + s + "\" (expected HH:MM)."};
    return h * 60 + m;
}

std::string trim(std::string s) {
    const char* ws = " \t\r\n";
    s.erase(0, s.find_first_not_of(ws));
    s.erase(s.find_last_not_of(ws) + 1);
    return s.find_first_not_of(ws) == std::string::npos ? "" : s;
}

std::string needStr(const json& j, const char* key, std::size_t maxLen = 120) {
    if (!j.contains(key) || !j[key].is_string()) throw ApiError{400, std::string("Missing field: ") + key};
    std::string v = trim(j[key].get<std::string>());
    if (v.empty()) throw ApiError{400, std::string("Field is empty: ") + key};
    if (v.size() > maxLen) throw ApiError{400, std::string("Field too long: ") + key};
    return v;
}

int needInt(const json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_number_integer()) throw ApiError{400, std::string("Missing number: ") + key};
    return j[key].get<int>();
}

int priorityOf(const json& j) {
    int p = j.value("priority", 2);
    if (p < 1 || p > 5) throw ApiError{400, "Priority must be between 1 and 5."};
    return p;
}

TimeSlot slotOf(const json& j) {
    int s = parseTime(needStr(j, "start")), e = parseTime(needStr(j, "end"));
    if (e <= s) throw ApiError{400, "End time must be after the start time."};
    return TimeSlot(s, e);
}

std::string needDate(const json& j, const char* key = "date") {
    std::string d = needStr(j, key);
    if (!CalendarService::validDate(d)) throw ApiError{400, "Invalid date (expected YYYY-MM-DD)."};
    return d;
}

json parseBody(const httplib::Request& req) {
    if (req.body.empty()) return json::object();
    json j = json::parse(req.body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) throw ApiError{400, "Request body must be a JSON object."};
    return j;
}

// ---- JSON views -----------------------------------------------------------
json slotJson(const TimeSlot& s) { return {{"start", fmtTime(s.start())}, {"end", fmtTime(s.end())}}; }

json eventJson(const Event& e, const std::string& date) {
    return {{"id", e.id()},       {"user", e.owner()},        {"title", e.title()},
            {"date", date},       {"start", fmtTime(e.slot().start())},
            {"end", fmtTime(e.slot().end())}, {"priority", e.priority()}};
}

json eventJson(const CalendarService::DatedEvent& d) { return eventJson(d.event, d.date); }

// Conflicting events don't carry a date; the caller knows it.
json conflictsJson(const std::vector<Event>& v, const std::string& date) {
    json a = json::array();
    for (const Event& e : v) a.push_back(eventJson(e, date));
    return a;
}

json taskJson(const CalendarService::PendingTask& p) {
    return {{"id", p.task.id},       {"user", p.task.owner},       {"title", p.task.title},
            {"date", p.date},        {"duration", p.task.duration},
            {"deadline", fmtTime(p.task.deadline)}, {"priority", p.task.priority}};
}

void send(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

void sendResult(httplib::Response& res, const CalendarService::Result& r, const std::string& date,
                int okStatus) {
    json body = {{"ok", r.ok}, {"id", r.id}, {"bumped", r.bumped}};
    if (!r.ok) {
        body["error"] = r.error;
        body["conflicts"] = conflictsJson(r.conflicts, date);
        body["suggestion"] = r.suggestion ? slotJson(*r.suggestion) : json(nullptr);
        send(res, r.conflicts.empty() ? 400 : 409, body);
        return;
    }
    send(res, okStatus, body);
}

std::string findFrontend(const std::string& hint, const char* argv0) {
    std::vector<fs::path> candidates;
    if (!hint.empty()) candidates.push_back(hint);
    candidates.push_back("frontend");
    candidates.push_back("../frontend");
    candidates.push_back(fs::path(argv0).parent_path() / ".." / "frontend");
    for (const auto& c : candidates)
        if (fs::exists(c / "index.html")) return fs::weakly_canonical(c).string();
    return "";
}

}  // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1", dataPath, frontendHint;
    bool dataGiven = false;
    int port = 8080;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << "Missing value for " << a << "\n"; std::exit(2); }
            return argv[++i];
        };
        if (a == "--port") port = std::atoi(next().c_str());
        else if (a == "--host") host = next();
        else if (a == "--data") { dataPath = next(); dataGiven = true; }
        else if (a == "--frontend") frontendHint = next();
        else if (a == "--help" || a == "-h") {
            std::cout << "Usage: chronosync_server [--port N] [--host H] [--data FILE|\"\"] [--frontend DIR]\n";
            return 0;
        } else { std::cerr << "Unknown option: " << a << "\n"; return 2; }
    }

    std::string frontend = findFrontend(frontendHint, argv[0]);
    // Default storage lives in <project root>/data no matter where the server is started from.
    if (!dataGiven)
        dataPath = (frontend.empty() ? fs::path("data") : fs::path(frontend).parent_path() / "data").string() +
                   "/chronosync.json";

    if (!dataPath.empty()) {
        fs::path parent = fs::path(dataPath).parent_path();
        if (!parent.empty()) fs::create_directories(parent);
    }
    CalendarService svc(dataPath);
    std::mutex mu;
    httplib::Server server;

    // Wrap every API handler: serialise access to the engine, map errors to JSON.
    auto api = [&](auto fn) {
        return [&mu, fn](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(mu);
            try {
                fn(req, res);
            } catch (const ApiError& e) {
                send(res, e.status, {{"error", e.message}});
            } catch (const json::exception&) {
                send(res, 400, {{"error", "Malformed request data."}});
            } catch (const std::exception& e) {
                send(res, 400, {{"error", e.what()}});
            }
        };
    };

    auto needUser = [&](const std::string& name) {
        if (!svc.hasUser(name)) throw ApiError{404, "Unknown user: " + name};
    };

    // ---- config / stats -----------------------------------------------------
    server.Get("/api/config", api([&](const auto&, auto& res) {
        send(res, 200, {{"dayStart", fmtTime(CalendarService::kDayStart)},
                        {"dayEnd", fmtTime(CalendarService::kDayEnd)}});
    }));

    server.Get("/api/stats", api([&](const auto&, auto& res) {
        auto s = svc.stats();
        send(res, 200, {{"events", s.events}, {"users", s.users}, {"pendingTasks", s.pendingTasks},
                        {"conflictsBlocked", s.conflictsBlocked}, {"bumps", s.bumps}});
    }));

    // ---- users -------------------------------------------------------------
    server.Get("/api/users", api([&](const auto&, auto& res) { send(res, 200, svc.users()); }));

    server.Post("/api/users", api([&](const auto& req, auto& res) {
        std::string name = needStr(parseBody(req), "name", 40);
        if (!svc.addUser(name)) throw ApiError{409, "A user named \"" + name + "\" already exists."};
        send(res, 201, {{"name", name}});
    }));

    server.Delete("/api/users", api([&](const auto& req, auto& res) {
        std::string name = req.get_param_value("name");
        if (!svc.removeUser(name)) throw ApiError{404, "Unknown user."};
        send(res, 200, {{"ok", true}});
    }));

    // ---- events ------------------------------------------------------------
    server.Get("/api/events", api([&](const auto& req, auto& res) {
        std::string user = req.get_param_value("user");
        needUser(user);
        json out = json::array();
        for (const auto& d : svc.events(user, req.get_param_value("from"), req.get_param_value("to")))
            out.push_back(eventJson(d));
        send(res, 200, out);
    }));

    server.Post("/api/events", api([&](const auto& req, auto& res) {
        json j = parseBody(req);
        std::string user = needStr(j, "user", 40), date = needDate(j);
        needUser(user);
        auto r = svc.addEvent(user, date, needStr(j, "title"), slotOf(j), priorityOf(j),
                              j.value("bump", false));
        sendResult(res, r, date, 201);
    }));

    server.Put(R"(/api/events/(\d+))", api([&](const auto& req, auto& res) {
        int id = std::stoi(req.matches[1]);
        json j = parseBody(req);
        std::string date = needDate(j);
        auto r = svc.updateEvent(id, needStr(j, "title"), date, slotOf(j), priorityOf(j),
                                 j.value("bump", false));
        if (!r.ok && r.error == "Event not found.") throw ApiError{404, r.error};
        sendResult(res, r, date, 200);
    }));

    server.Delete(R"(/api/events/(\d+))", api([&](const auto& req, auto& res) {
        if (!svc.removeEvent(std::stoi(req.matches[1]))) throw ApiError{404, "Event not found."};
        send(res, 200, {{"ok", true}});
    }));

    // ---- conflict detection / free time ------------------------------------
    server.Post("/api/check", api([&](const auto& req, auto& res) {
        json j = parseBody(req);
        std::string user = needStr(j, "user", 40), date = needDate(j);
        needUser(user);
        auto rep = svc.check(user, date, slotOf(j), priorityOf(j), j.value("ignoreId", 0));
        send(res, 200, {{"conflict", !rep.conflicts.empty()},
                        {"conflicts", conflictsJson(rep.conflicts, date)},
                        {"suggestion", rep.suggestion ? slotJson(*rep.suggestion) : json(nullptr)},
                        {"canBump", rep.canBump}});
    }));

    server.Get("/api/free", api([&](const auto& req, auto& res) {
        std::string user = req.get_param_value("user"), date = req.get_param_value("date");
        needUser(user);
        if (!CalendarService::validDate(date)) throw ApiError{400, "Invalid date."};
        json out = json::array();
        for (const TimeSlot& s : svc.freeSlots(user, date)) out.push_back(slotJson(s));
        send(res, 200, out);
    }));

    server.Post("/api/common", api([&](const auto& req, auto& res) {
        json j = parseBody(req);
        std::string date = needDate(j);
        int duration = needInt(j, "duration");
        if (duration < 5 || duration > 12 * 60) throw ApiError{400, "Duration must be 5-720 minutes."};
        std::vector<std::string> names = j.value("users", std::vector<std::string>{});
        if (names.empty()) throw ApiError{400, "Pick at least one person."};
        for (const auto& n : names) needUser(n);
        json out = json::array();
        for (const TimeSlot& s : svc.commonSlots(names, date, duration)) out.push_back(slotJson(s));
        send(res, 200, {{"slots", out}});
    }));

    server.Post("/api/group-events", api([&](const auto& req, auto& res) {
        json j = parseBody(req);
        std::string date = needDate(j);
        std::vector<std::string> names = j.value("users", std::vector<std::string>{});
        std::map<std::string, std::vector<Event>> busy;
        auto r = svc.addGroupEvent(names, date, needStr(j, "title"), slotOf(j), priorityOf(j), &busy);
        if (!r.ok) {
            json b = json::object();
            for (const auto& [n, v] : busy) b[n] = conflictsJson(v, date);
            send(res, busy.empty() ? 400 : 409, {{"ok", false}, {"error", r.error}, {"busy", b}});
            return;
        }
        send(res, 201, {{"ok", true}, {"count", names.size()}});
    }));

    // ---- task queue ---------------------------------------------------------
    server.Get("/api/tasks", api([&](const auto& req, auto& res) {
        std::string user = req.get_param_value("user");
        if (!user.empty()) needUser(user);
        std::string strategy = req.has_param("strategy") ? req.get_param_value("strategy") : "EDF";
        json out = json::array();
        for (const auto& t : svc.tasks(user, strategy)) out.push_back(taskJson(t));
        send(res, 200, out);
    }));

    server.Post("/api/tasks", api([&](const auto& req, auto& res) {
        json j = parseBody(req);
        std::string user = needStr(j, "user", 40);
        needUser(user);
        int duration = needInt(j, "duration");
        if (duration < 5 || duration > 12 * 60) throw ApiError{400, "Duration must be 5-720 minutes."};
        int deadline = parseTime(needStr(j, "deadline"));
        if (deadline < CalendarService::kDayStart + duration)
            throw ApiError{400, "Deadline is too early: the task needs " + std::to_string(duration) +
                                    " minutes starting from " + fmtTime(CalendarService::kDayStart) + "."};
        int id = svc.addTask(user, needDate(j), needStr(j, "title"), duration, deadline, priorityOf(j));
        send(res, 201, {{"id", id}});
    }));

    server.Delete(R"(/api/tasks/(\d+))", api([&](const auto& req, auto& res) {
        if (!svc.removeTask(std::stoi(req.matches[1]))) throw ApiError{404, "Task not found."};
        send(res, 200, {{"ok", true}});
    }));

    server.Post("/api/schedule", api([&](const auto& req, auto& res) {
        json j = parseBody(req);
        std::string user = needStr(j, "user", 40);
        needUser(user);
        std::string strategy = j.value("strategy", "EDF");
        if (strategy != "EDF" && strategy != "SDF") throw ApiError{400, "Unknown strategy."};
        auto out = svc.autoSchedule(user, strategy);
        json placed = json::array(), unplaced = json::array();
        for (const auto& p : out.placed)
            placed.push_back({{"id", p.task.id}, {"title", p.task.title}, {"date", p.date},
                              {"start", fmtTime(p.slot.start())}, {"end", fmtTime(p.slot.end())}});
        for (const auto& u : out.unplaced) unplaced.push_back(taskJson(u));
        send(res, 200, {{"strategy", out.strategy}, {"placed", placed}, {"unplaced", unplaced}});
    }));

    // ---- notifications (Observer log) -----------------------------------------
    server.Get("/api/notifications", api([&](const auto& req, auto& res) {
        std::string user = req.get_param_value("user");
        json out = json::array();
        for (const auto& n : svc.notifications(200)) {
            if (!user.empty() && n.owner != user) continue;
            out.push_back({{"owner", n.owner},         {"title", n.title},
                           {"date", n.date},           {"reason", n.reason},
                           {"time", n.time},           {"rescheduled", n.rescheduled},
                           {"oldStart", fmtTime(n.oldSlot.start())}, {"oldEnd", fmtTime(n.oldSlot.end())},
                           {"newStart", fmtTime(n.newSlot.start())}, {"newEnd", fmtTime(n.newSlot.end())}});
            if (out.size() >= 50) break;
        }
        send(res, 200, out);
    }));

    server.Delete("/api/notifications", api([&](const auto&, auto& res) {
        svc.clearNotifications();
        send(res, 200, {{"ok", true}});
    }));

    // ---- demo data ----------------------------------------------------------
    server.Post("/api/demo", api([&](const auto& req, auto& res) {
        json j = parseBody(req);
        std::vector<std::string> dates = j.value("dates", std::vector<std::string>{});
        if (dates.size() < 5) throw ApiError{400, "Provide 5 dates."};
        for (const auto& d : dates)
            if (!CalendarService::validDate(d)) throw ApiError{400, "Invalid date."};
        svc.seedDemo(dates);
        send(res, 200, {{"ok", true}});
    }));

    // ---- errors + static frontend -----------------------------------------------
    server.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
        send(res, 500, {{"error", "Internal server error."}});
    });
    server.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        if (req.path.rfind("/api/", 0) == 0 && res.body.empty())
            send(res, res.status, {{"error", "No such API endpoint."}});
    });

    if (frontend.empty()) {
        std::cerr << "warning: frontend/index.html not found; only the API will be served.\n"
                     "         Run from the project root or pass --frontend DIR.\n";
    } else {
        server.set_mount_point("/", frontend);
        server.set_file_request_handler([](const httplib::Request&, httplib::Response& res) {
            res.set_header("Cache-Control", "no-cache");
        });
    }

    std::cout << "ChronoSync server\n"
              << "  data     : " << (dataPath.empty() ? "(memory only)" : dataPath) << "\n"
              << "  frontend : " << (frontend.empty() ? "(none)" : frontend) << "\n"
              << "  open     : http://" << (host == "0.0.0.0" ? "localhost" : host) << ":" << port << "\n"
              << "Press Ctrl+C to stop.\n" << std::flush;

    if (!server.listen(host, port)) {
        std::cerr << "error: could not listen on " << host << ":" << port << " (port in use?)\n";
        return 1;
    }
    return 0;
}
