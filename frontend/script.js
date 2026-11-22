/* ChronoSync frontend.
 * All scheduling logic (conflict detection, free slots, bumping, task placement)
 * lives in the C++ engine; this file only talks to its REST API and draws the result. */
"use strict";

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
const $ = (sel, root = document) => root.querySelector(sel);
const pad = n => String(n).padStart(2, "0");
const iso = d => `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
const fromISO = s => { const [y, m, d] = s.split("-").map(Number); return new Date(y, m - 1, d); };
const addDays = (d, n) => { const x = new Date(d); x.setDate(x.getDate() + n); return x; };
const mondayOf = d => { const x = new Date(d.getFullYear(), d.getMonth(), d.getDate()); x.setDate(x.getDate() - ((x.getDay() + 6) % 7)); return x; };
const toMin = t => { const [h, m] = t.split(":").map(Number); return h * 60 + m; };
const fmtMin = n => `${pad(Math.floor(n / 60))}:${pad(n % 60)}`;
const fmt12 = t => { const [h, m] = t.split(":").map(Number); return `${h % 12 || 12}:${pad(m)} ${h % 24 >= 12 ? "PM" : "AM"}`; };
const range12 = (a, b) => `${fmt12(a)} – ${fmt12(b)}`;
const hours = min => { const h = min / 60; return Number.isInteger(h) ? `${h}h` : `${h.toFixed(1)}h`; };
const dayName = (d, opts = { weekday: "short", month: "short", day: "numeric" }) => d.toLocaleDateString("en", opts);

// Tiny DOM builder: h("div", {class: "x", onclick: fn}, child, "text", ...)
function h(tag, attrs = {}, ...kids) {
    const el = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs || {})) {
        if (v == null || v === false) continue;
        if (k.startsWith("on")) el.addEventListener(k.slice(2), v);
        else if (k === "class") el.className = v;
        else if (k === "text") el.textContent = v;
        else el.setAttribute(k, v === true ? "" : v);
    }
    for (const kid of kids.flat()) if (kid != null && kid !== false) el.append(kid.nodeType ? kid : document.createTextNode(kid));
    return el;
}

const PRIORITIES = [
    [1, "Low"], [2, "Normal"], [3, "High"], [4, "Urgent"], [5, "Critical"],
];
const priorityName = p => (PRIORITIES.find(x => x[0] === p) || ["", "P" + p])[1];

// ---------------------------------------------------------------------------
// API client
// ---------------------------------------------------------------------------
async function api(method, path, body) {
    let res;
    try {
        res = await fetch("/api" + path, {
            method,
            headers: body ? { "Content-Type": "application/json" } : undefined,
            body: body ? JSON.stringify(body) : undefined,
        });
    } catch (e) {
        setOnline(false);
        throw Object.assign(new Error("Cannot reach the server."), { offline: true });
    }
    setOnline(true);
    let data = null;
    try { data = await res.json(); } catch { /* empty body */ }
    if (!res.ok) throw Object.assign(new Error((data && data.error) || res.statusText), { status: res.status, data });
    return data;
}
const qs = o => "?" + new URLSearchParams(o).toString();

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
const state = {
    users: [],
    user: localStorage.getItem("chronosync-user") || "",   // only a UI preference
    events: [],
    tasks: [],
    notices: [],
    stats: { conflictsBlocked: 0, bumps: 0 },
    freeByDate: {},            // "YYYY-MM-DD" -> [{start,end}] for the visible week + today
    weekStart: mondayOf(new Date()),
    strategy: "EDF",
    showFree: false,
    showPast: false,
    unplacedIds: new Set(),
    teamSelected: new Set(),
    teamSlot: null,
    online: true,
};

const HOUR_PX = 56;

// ---------------------------------------------------------------------------
// Online / offline indicator, toasts
// ---------------------------------------------------------------------------
function setOnline(ok) {
    state.online = ok;
    const s = $("#status");
    s.textContent = ok ? "● Engine connected" : "● Offline";
    s.classList.toggle("offline", !ok);
    $("#offline-banner").hidden = ok;
    if (!ok && location.protocol === "file:")
        $("#offline-detail").textContent = " This page must be opened through the server: run make serve, then visit http://localhost:8080.";
}

function toast(message, kind = "info") {
    const t = h("div", { class: `toast ${kind}`, role: "status" }, message);
    $("#toasts").append(t);
    setTimeout(() => { t.classList.add("leaving"); setTimeout(() => t.remove(), 300); }, kind === "error" ? 6000 : 4000);
}

function errorToast(e) {
    if (!e.offline) toast(e.message, "error");
}

// ---------------------------------------------------------------------------
// Data loading
// ---------------------------------------------------------------------------
async function loadUsers() {
    state.users = await api("GET", "/users");
    if (!state.users.includes(state.user)) state.user = state.users[0] || "";
    localStorage.setItem("chronosync-user", state.user);
}

async function loadFree() {
    // Free time comes from the engine (UserCalendar::freeSlots) for each visible day.
    const dates = new Set([iso(new Date())]);
    for (let i = 0; i < 7; i++) dates.add(iso(addDays(state.weekStart, i)));
    const out = {};
    await Promise.all([...dates].map(async d => {
        out[d] = await api("GET", "/free" + qs({ user: state.user, date: d }));
    }));
    state.freeByDate = out;
}

async function refreshAll() {
    try {
        await loadUsers();
    } catch (e) { errorToast(e); return; }
    renderUsers();
    const hasUsers = state.users.length > 0;
    $("#welcome").hidden = hasUsers;
    $("#app").hidden = !hasUsers;
    $("#new-event-btn").hidden = !hasUsers;
    if (!hasUsers) return;

    try {
        const [events, tasks, notices, stats] = await Promise.all([
            api("GET", "/events" + qs({ user: state.user })),
            api("GET", "/tasks" + qs({ user: state.user, strategy: state.strategy })),
            api("GET", "/notifications" + qs({ user: state.user })),
            api("GET", "/stats"),
        ]);
        Object.assign(state, { events, tasks, notices, stats });
        await loadFree();
    } catch (e) { errorToast(e); return; }
    renderAll();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
function renderAll() {
    renderStats();
    renderCalendar();
    renderActivity();
    renderUpcoming();
    renderEventList();
    renderTasks();
    renderTeamUsers();
}

function renderUsers() {
    const sel = $("#user-select");
    sel.replaceChildren(...state.users.map(u => h("option", { value: u, selected: u === state.user }, u)));
    $("#avatar").textContent = state.user ? state.user.slice(0, 2).toUpperCase() : "CS";
    $("#who-name").textContent = state.user || "—";
    $("#remove-user-btn").disabled = state.users.length === 0;
}

function renderStats() {
    const today = iso(new Date());
    const todays = state.events.filter(e => e.date === today);
    $("#stat-total").textContent = state.events.length;
    $("#stat-total-sub").textContent = `on ${state.user}'s calendar`;
    $("#stat-today").textContent = todays.length;
    const nowMin = new Date().getHours() * 60 + new Date().getMinutes();
    const next = todays.find(e => toMin(e.end) > nowMin);
    $("#stat-today-sub").textContent = next ? `Next: ${next.title} at ${fmt12(next.start)}` : todays.length ? "All done for today" : "Nothing scheduled";

    const free = state.freeByDate[today] || [];
    const minutes = free.reduce((s, f) => s + toMin(f.end) - toMin(f.start), 0);
    $("#stat-free").textContent = hours(minutes);
    $("#stat-free-sub").textContent = `${free.length} open slot${free.length === 1 ? "" : "s"}, 8 AM – 8 PM`;

    $("#stat-conflicts").textContent = state.stats.conflictsBlocked;
    $("#stat-conflicts-sub").textContent = state.stats.bumps
        ? `${state.stats.bumps} bumped & rescheduled` : "Double-bookings prevented";
}

function renderCalendar() {
    const root = $("#week");
    const days = [...Array(7)].map((_, i) => addDays(state.weekStart, i));
    const first = iso(days[0]), last = iso(days[6]);
    $("#week-label").textContent = `${dayName(days[0], { month: "short", day: "numeric" })} – ${dayName(days[6], { month: "short", day: "numeric" })}`.toUpperCase();

    const week = state.events.filter(e => e.date >= first && e.date <= last);
    let startH = 8, endH = 20;
    for (const e of week) {
        startH = Math.min(startH, Math.floor(toMin(e.start) / 60));
        endH = Math.max(endH, Math.ceil(toMin(e.end) / 60));
    }
    const total = (endH - startH) * HOUR_PX;
    const todayISO = iso(new Date());
    root.style.setProperty("--hour", HOUR_PX + "px");
    root.replaceChildren();

    // header row
    root.append(h("div", { class: "day-heading corner" }));
    for (const d of days)
        root.append(h("div", { class: "day-heading" + (iso(d) === todayISO ? " today" : "") },
            d.toLocaleDateString("en", { weekday: "short" }).toUpperCase(), " ", h("b", {}, d.getDate())));

    // time gutter
    const gutter = h("div", { class: "time-col", style: `height:${total}px` });
    for (let hr = startH; hr < endH; hr++)
        gutter.append(h("div", { class: "time", style: `top:${(hr - startH) * HOUR_PX}px` }, fmt12(`${hr}:00`).replace(":00", "")));
    root.append(gutter);

    // day columns
    for (const d of days) {
        const date = iso(d);
        const col = h("div", { class: "day-col" + (date === todayISO ? " today" : ""), style: `height:${total}px`, "data-date": date });

        if (state.showFree)
            for (const f of state.freeByDate[date] || []) {
                const top = (toMin(f.start) - startH * 60) / 60 * HOUR_PX, ht = (toMin(f.end) - toMin(f.start)) / 60 * HOUR_PX;
                if (top + ht <= 0 || top >= total) continue;
                col.append(h("div", { class: "free-block", style: `top:${Math.max(0, top)}px;height:${Math.min(total, top + ht) - Math.max(0, top)}px` }));
            }

        for (const e of week.filter(x => x.date === date)) {
            const top = (toMin(e.start) - startH * 60) / 60 * HOUR_PX;
            const ht = Math.max(22, (toMin(e.end) - toMin(e.start)) / 60 * HOUR_PX - 2);
            col.append(h("button", {
                type: "button", class: `ev p${e.priority}${ht < 40 ? " tight" : ""}`,
                style: `top:${top}px;height:${ht}px`,
                title: `${e.title}\n${range12(e.start, e.end)} · ${priorityName(e.priority)} priority`,
                onclick: ev => { ev.stopPropagation(); openEventDialog({ event: e }); },
            }, h("strong", {}, e.title), h("span", {}, range12(e.start, e.end))));
        }

        if (date === todayISO) {
            const now = new Date(), m = now.getHours() * 60 + now.getMinutes();
            if (m >= startH * 60 && m < endH * 60)
                col.append(h("div", { class: "now-line", style: `top:${(m - startH * 60) / 60 * HOUR_PX}px` }));
        }

        col.addEventListener("click", ev => {
            if (ev.target !== col && !ev.target.classList.contains("free-block")) return;
            const y = ev.clientY - col.getBoundingClientRect().top;
            const start = Math.min(startH * 60 + Math.floor(y / HOUR_PX * 2) * 30, 23 * 60);
            openEventDialog({ date, start: fmtMin(start), end: fmtMin(Math.min(start + 60, 24 * 60 - 1)) });
        });
        root.append(col);
    }
}

function renderActivity() {
    const list = $("#activity-list");
    list.replaceChildren();
    $("#activity-badge").textContent = `${state.notices.length} alert${state.notices.length === 1 ? "" : "s"}`;
    $("#activity-badge").classList.toggle("ok", state.notices.length === 0);
    $("#clear-activity").hidden = state.notices.length === 0;

    if (!state.notices.length) {
        list.append(h("div", { class: "alert-card ok" },
            h("span", { class: "alert-icon" }, "✓"),
            h("div", {}, h("strong", {}, "No scheduling conflicts"),
                h("p", {}, "Overlapping events are blocked automatically. If a higher-priority event bumps one of yours, you'll be notified here."))));
        return;
    }
    for (const n of state.notices.slice(0, 6)) {
        const when = n.date ? dayName(fromISO(n.date), { weekday: "short", month: "short", day: "numeric" }) : "";
        list.append(h("div", { class: "alert-card" },
            h("span", { class: "alert-icon" }, "!"),
            h("div", {},
                h("strong", {}, n.rescheduled ? `“${n.title}” was rescheduled` : `“${n.title}” was dropped`),
                h("p", {}, n.rescheduled
                    ? `${range12(n.oldStart, n.oldEnd)} → ${range12(n.newStart, n.newEnd)}`
                    : `${range12(n.oldStart, n.oldEnd)} · no free slot left, moved to your task queue`),
                h("small", {}, `${when} · ${n.reason}`))));
    }
}

function eventRow(e, { actions = false } = {}) {
    const d = fromISO(e.date);
    const row = h("div", { class: "event-row", tabindex: 0, role: "button", onclick: () => openEventDialog({ event: e }),
        onkeydown: ev => { if (ev.key === "Enter") openEventDialog({ event: e }); } },
        h("div", { class: "event-date" }, h("b", {}, pad(d.getDate())), h("span", {}, d.toLocaleDateString("en", { month: "short" }).toUpperCase())),
        h("div", { class: "event-info" }, h("strong", {}, e.title), h("p", {}, `${dayName(d, { weekday: "short" })} · ${range12(e.start, e.end)}`)),
        h("span", { class: `badge p${e.priority}` }, priorityName(e.priority)));
    if (actions)
        row.append(
            h("button", { type: "button", class: "edit-btn", onclick: ev => { ev.stopPropagation(); openEventDialog({ event: e }); } }, "Edit"),
            h("button", { type: "button", class: "delete-btn", onclick: ev => { ev.stopPropagation(); deleteEvent(e); } }, "Delete"));
    return row;
}

const nowKey = () => { const n = new Date(); return iso(n) + " " + fmtMin(n.getHours() * 60 + n.getMinutes()); };

function renderUpcoming() {
    const list = $("#upcoming-list");
    list.replaceChildren();
    const now = nowKey();
    const up = state.events.filter(e => `${e.date} ${e.end}` > now).slice(0, 5);
    if (!up.length) list.append(h("p", { class: "empty" }, "No upcoming events. Click “+ Add” to schedule one."));
    for (const e of up) list.append(eventRow(e));
}

function renderEventList() {
    const list = $("#event-list");
    list.replaceChildren();
    const now = nowKey();
    const shown = state.events.filter(e => state.showPast || `${e.date} ${e.end}` > now);
    $("#events-sub").textContent = `${shown.length} event${shown.length === 1 ? "" : "s"} on ${state.user}'s calendar`;
    if (!shown.length) { list.append(h("p", { class: "empty" }, "Nothing here yet.")); return; }

    let lastDate = "";
    for (const e of shown) {
        if (e.date !== lastDate) {
            lastDate = e.date;
            const d = fromISO(e.date), isToday = e.date === iso(new Date());
            list.append(h("h3", { class: "date-group" }, dayName(d, { weekday: "long", month: "long", day: "numeric" }), isToday ? h("span", { class: "pill" }, "Today") : null));
        }
        list.append(eventRow(e, { actions: true }));
    }
}

function renderTasks() {
    const list = $("#task-list");
    list.replaceChildren();
    $("#schedule-btn").disabled = state.tasks.length === 0;
    $("#schedule-btn").textContent = state.tasks.length ? `⚡ Auto-schedule ${state.tasks.length} task${state.tasks.length === 1 ? "" : "s"}` : "⚡ Auto-schedule queue";
    for (const b of document.querySelectorAll("#strategy button")) b.classList.toggle("active", b.dataset.strategy === state.strategy);

    if (!state.tasks.length) { list.append(h("p", { class: "empty" }, "The queue is empty. Add a task above.")); return; }
    state.tasks.forEach((t, i) => {
        const stuck = state.unplacedIds.has(t.id);
        list.append(h("div", { class: "task-row" + (stuck ? " stuck" : "") },
            h("span", { class: "rank" }, i + 1),
            h("div", { class: "event-info" },
                h("strong", {}, t.title),
                h("p", {}, `${dayName(fromISO(t.date))} · ${t.duration} min · finish by ${fmt12(t.deadline)}`),
                stuck ? h("small", { class: "warn" }, "No free slot before the deadline. Free up time or extend the deadline.") : null),
            h("span", { class: `badge p${t.priority}` }, priorityName(t.priority)),
            h("button", { type: "button", class: "icon-btn danger", title: "Remove task", "aria-label": `Remove ${t.title}`, onclick: () => removeTask(t) }, "✕")));
    });
}

function renderTeamUsers() {
    const box = $("#team-users");
    box.replaceChildren();
    for (const u of state.users) {
        const id = "tu-" + u.replace(/\W/g, "_");
        const cb = h("input", { type: "checkbox", id, checked: state.teamSelected.has(u), onchange: ev => {
            ev.target.checked ? state.teamSelected.add(u) : state.teamSelected.delete(u);
        } });
        box.append(h("label", { class: "chip", for: id }, cb, u));
    }
}

// ---------------------------------------------------------------------------
// Event dialog (create / edit) with live conflict detection
// ---------------------------------------------------------------------------
const dlg = $("#event-dialog");
let dialogState = { id: null, bump: false, timer: null, seq: 0 };

function fillPrioritySelect(sel, value = 2) {
    sel.replaceChildren(...PRIORITIES.map(([v, name]) => h("option", { value: v, selected: v === value }, `${v} · ${name}`)));
}

function openEventDialog({ event = null, date = null, start = null, end = null } = {}) {
    if (!state.user) return;
    const editing = !!event;
    dialogState = { id: editing ? event.id : null, bump: false, timer: null, seq: 0 };
    $("#dialog-title").textContent = editing ? "Edit Event" : "New Event";
    $("#event-submit").textContent = editing ? "Save changes" : "Create Event";
    $("#event-delete").hidden = !editing;
    $("#event-title").value = editing ? event.title : "";
    $("#event-date").value = editing ? event.date : date || iso(new Date());
    fillPrioritySelect($("#event-priority"), editing ? event.priority : 2);

    let s = start, e = end;
    if (!editing && !s) {
        const n = new Date();
        const m = Math.min(Math.max(Math.ceil((n.getHours() * 60 + n.getMinutes() + 1) / 30) * 30, 9 * 60), 22 * 60);
        s = fmtMin(m); e = fmtMin(m + 60);
    }
    $("#event-start").value = editing ? event.start : s;
    $("#event-end").value = editing ? event.end : e;
    $("#check-box").replaceChildren();
    if (!dlg.open) dlg.showModal();
    runCheck();
    if (!editing) $("#event-title").focus();
}

function closeDialog() { if (dlg.open) dlg.close(); }

function formValues() {
    return {
        title: $("#event-title").value.trim(),
        date: $("#event-date").value,
        start: $("#event-start").value,
        end: $("#event-end").value,
        priority: Number($("#event-priority").value),
    };
}

function scheduleCheck() { clearTimeout(dialogState.timer); dialogState.timer = setTimeout(runCheck, 200); }

async function runCheck() {
    const v = formValues();
    const box = $("#check-box");
    const submit = $("#event-submit");
    if (!v.date || !v.start || !v.end) { box.replaceChildren(); return; }
    if (toMin(v.end) <= toMin(v.start)) {
        box.replaceChildren(h("div", { class: "check bad" }, "End time must be after the start time."));
        submit.disabled = true;
        return;
    }
    const seq = ++dialogState.seq;
    let r;
    try {
        r = await api("POST", "/check", { user: state.user, date: v.date, start: v.start, end: v.end, priority: v.priority, ignoreId: dialogState.id || 0 });
    } catch (e) { return; }
    if (seq !== dialogState.seq) return;  // stale answer

    if (!r.conflict) {
        dialogState.bump = false;
        box.replaceChildren(h("div", { class: "check good" }, "✓ No conflicts. This time is free."));
        submit.disabled = false;
        return;
    }

    const items = h("ul", {}, r.conflicts.map(c => h("li", {}, h("strong", {}, c.title), ` · ${range12(c.start, c.end)} · ${priorityName(c.priority)}`)));
    const parts = [h("div", { class: "check-title" }, `⚠ Conflicts with ${r.conflicts.length} event${r.conflicts.length === 1 ? "" : "s"}`), items];
    if (r.suggestion) {
        parts.push(h("button", { type: "button", class: "ghost-btn small", onclick: () => {
            $("#event-start").value = r.suggestion.start;
            $("#event-end").value = r.suggestion.end;
            runCheck();
        } }, `Use next free slot: ${range12(r.suggestion.start, r.suggestion.end)}`));
    }
    if (r.canBump) {
        parts.push(h("label", { class: "bump" },
            h("input", { type: "checkbox", id: "bump-cb", checked: dialogState.bump, onchange: ev => { dialogState.bump = ev.target.checked; submit.disabled = !dialogState.bump; } }),
            h("span", {}, `This event has higher priority. Bump the conflicting event${r.conflicts.length === 1 ? "" : "s"} and reschedule ${r.conflicts.length === 1 ? "it" : "them"} automatically.`)));
        submit.disabled = !dialogState.bump;
    } else {
        parts.push(h("p", { class: "hint" }, "To bump an event, this one needs a strictly higher priority than every event it overlaps."));
        submit.disabled = true;
    }
    box.replaceChildren(h("div", { class: "check bad" }, parts));
}

async function submitEvent(ev) {
    ev.preventDefault();
    const v = formValues();
    if (!v.title) { $("#event-title").focus(); toast("Please give the event a name.", "error"); return; }
    if (!v.date || !v.start || !v.end) { toast("Please fill in the date and times.", "error"); return; }
    if (toMin(v.end) <= toMin(v.start)) { toast("End time must be after the start time.", "error"); return; }

    const body = { ...v, bump: dialogState.bump };
    const editing = dialogState.id !== null;
    try {
        const r = editing
            ? await api("PUT", `/events/${dialogState.id}`, body)
            : await api("POST", "/events", { ...body, user: state.user });
        closeDialog();
        // Jump to the week that now contains the event so the user sees it.
        state.weekStart = mondayOf(fromISO(v.date));
        await refreshAll();
        toast(r.bumped ? `Saved “${v.title}”. Conflicting events were rescheduled (see Activity).`
            : editing ? `Updated “${v.title}”.` : `Added “${v.title}”.`, "success");
    } catch (e) {
        if (e.status === 409) { runCheck(); toast(e.message, "error"); }
        else errorToast(e);
    }
}

async function deleteEvent(e) {
    if (!confirm(`Delete “${e.title}” (${dayName(fromISO(e.date))}, ${range12(e.start, e.end)})?`)) return;
    try {
        await api("DELETE", `/events/${e.id}`);
        closeDialog();
        await refreshAll();
        toast(`Deleted “${e.title}”.`, "success");
    } catch (err) { errorToast(err); }
}

// ---------------------------------------------------------------------------
// Tasks (priority queue + strategy)
// ---------------------------------------------------------------------------
async function addTask(ev) {
    ev.preventDefault();
    const title = $("#task-title").value.trim();
    if (!title) return;
    try {
        await api("POST", "/tasks", {
            user: state.user, title, date: $("#task-date").value,
            duration: Number($("#task-duration").value), deadline: $("#task-deadline").value,
            priority: Number($("#task-priority").value),
        });
        $("#task-title").value = "";
        await refreshAll();
        toast(`Queued “${title}”.`, "success");
    } catch (e) { errorToast(e); }
}

async function removeTask(t) {
    try { await api("DELETE", `/tasks/${t.id}`); state.unplacedIds.delete(t.id); await refreshAll(); }
    catch (e) { errorToast(e); }
}

async function runSchedule() {
    try {
        const r = await api("POST", "/schedule", { user: state.user, strategy: state.strategy });
        state.unplacedIds = new Set(r.unplaced.map(t => t.id));
        if (r.placed.length) state.weekStart = mondayOf(fromISO(r.placed[0].date));
        await refreshAll();
        if (r.placed.length && !r.unplaced.length) toast(`Scheduled ${r.placed.length} task${r.placed.length === 1 ? "" : "s"} (${r.strategy}).`, "success");
        else if (r.placed.length) toast(`Scheduled ${r.placed.length}; ${r.unplaced.length} didn't fit before their deadline.`, "info");
        else toast("None of the queued tasks fit before their deadlines.", "error");
        if (r.placed.length) $("#calendar").scrollIntoView({ behavior: "smooth", block: "start" });
    } catch (e) { errorToast(e); }
}

// ---------------------------------------------------------------------------
// Team slots
// ---------------------------------------------------------------------------
async function findTeamSlots(ev) {
    ev.preventDefault();
    const users = [...state.teamSelected].filter(u => state.users.includes(u));
    const out = $("#team-results");
    if (!users.length) { toast("Pick at least one person.", "error"); return; }
    try {
        const date = $("#team-date").value, duration = Number($("#team-duration").value);
        const r = await api("POST", "/common", { users, date, duration });
        state.teamSlot = null;
        out.replaceChildren();
        if (!r.slots.length) {
            out.append(h("p", { class: "empty" }, `No ${duration}-minute window where ${users.join(", ")} ${users.length === 1 ? "is" : "are all"} free on ${dayName(fromISO(date))} (8 AM – 8 PM).`));
            return;
        }
        out.append(h("p", { class: "muted" }, `Free for ${users.join(", ")} on ${dayName(fromISO(date), { weekday: "long", month: "short", day: "numeric" })}:`));
        const chips = h("div", { class: "chip-row" });
        for (const s of r.slots) {
            const b = h("button", { type: "button", class: "slot-chip", onclick: () => {
                state.teamSlot = s;
                chips.querySelectorAll(".slot-chip").forEach(c => c.classList.toggle("active", c === b));
                showTeamBooking(users, date, s);
            } }, range12(s.start, s.end));
            chips.append(b);
        }
        out.append(chips, h("div", { id: "team-book" }));
    } catch (e) { errorToast(e); }
}

function showTeamBooking(users, date, slot) {
    const box = $("#team-book");
    const title = h("input", { type: "text", placeholder: "Meeting name, e.g. Project sync", maxlength: "120" });
    const prio = h("select", {});
    fillPrioritySelect(prio, 3);
    const btn = h("button", { type: "button", class: "primary-btn", onclick: async () => {
        const name = title.value.trim();
        if (!name) { title.focus(); toast("Give the meeting a name.", "error"); return; }
        try {
            await api("POST", "/group-events", { users, title: name, date, start: slot.start, end: slot.end, priority: Number(prio.value) });
            state.weekStart = mondayOf(fromISO(date));
            $("#team-results").replaceChildren(h("p", { class: "check good" }, `✓ “${name}” booked at ${range12(slot.start, slot.end)} for ${users.join(", ")}.`));
            await refreshAll();
        } catch (e) {
            if (e.status === 409) toast("Someone's calendar changed. Search again.", "error"); else errorToast(e);
        }
    } }, `Schedule for ${users.length} ${users.length === 1 ? "person" : "people"}`);
    box.replaceChildren(h("div", { class: "book-row" }, title, prio, btn));
    title.focus();
}

// ---------------------------------------------------------------------------
// Users & demo data
// ---------------------------------------------------------------------------
async function createUser(name) {
    try {
        await api("POST", "/users", { name });
        state.user = name;
        state.teamSelected = new Set([name]);
        localStorage.setItem("chronosync-user", name);
        await refreshAll();
        toast(`Created calendar for ${name}.`, "success");
        return true;
    } catch (e) { errorToast(e); return false; }
}

async function loadDemo() {
    // Seed Mon-Fri of this week, or of next week once it's Thursday or later so there is upcoming data.
    const today = new Date();
    const mon = addDays(mondayOf(today), (today.getDay() + 6) % 7 >= 3 ? 7 : 0);
    try {
        await api("POST", "/demo", { dates: [0, 1, 2, 3, 4].map(i => iso(addDays(mon, i))) });
        state.weekStart = mon;
        state.user = "Alice";
        state.teamSelected = new Set(["Alice", "Bob"]);
        await refreshAll();
        toast("Loaded a sample week for Alice and Bob. Try double-booking Alice on Monday at 9:30!", "success");
    } catch (e) { errorToast(e); }
}

// ---------------------------------------------------------------------------
// Wiring
// ---------------------------------------------------------------------------
function init() {
    fillPrioritySelect($("#task-priority"), 2);
    $("#task-date").value = $("#team-date").value = iso(new Date());

    // sidebar nav: highlight the section in view
    const links = [...document.querySelectorAll("#nav a")];
    links.forEach(a => a.addEventListener("click", () => { links.forEach(l => l.classList.toggle("active", l === a)); }));

    $("#new-event-btn").addEventListener("click", () => openEventDialog());
    $("#add-upcoming").addEventListener("click", () => openEventDialog());

    $("#prev-week").addEventListener("click", async () => { state.weekStart = addDays(state.weekStart, -7); await loadFree(); renderCalendar(); });
    $("#next-week").addEventListener("click", async () => { state.weekStart = addDays(state.weekStart, 7); await loadFree(); renderCalendar(); });
    $("#today-btn").addEventListener("click", async () => { state.weekStart = mondayOf(new Date()); await loadFree(); renderCalendar(); });
    $("#show-free").addEventListener("change", e => { state.showFree = e.target.checked; renderCalendar(); });
    $("#show-past").addEventListener("change", e => { state.showPast = e.target.checked; renderEventList(); });

    // event dialog
    $("#event-form").addEventListener("submit", submitEvent);
    for (const id of ["event-date", "event-start", "event-end", "event-priority"])
        $("#" + id).addEventListener("input", () => { dialogState.bump = false; scheduleCheck(); });
    $("#event-cancel").addEventListener("click", closeDialog);
    $("#dialog-close").addEventListener("click", closeDialog);
    $("#event-delete").addEventListener("click", () => {
        const e = state.events.find(x => x.id === dialogState.id);
        if (e) deleteEvent(e);
    });
    dlg.addEventListener("click", e => { if (e.target === dlg) closeDialog(); });  // click on backdrop

    // users
    $("#user-select").addEventListener("change", async e => {
        state.user = e.target.value;
        localStorage.setItem("chronosync-user", state.user);
        state.unplacedIds = new Set();
        await refreshAll();
    });
    const udlg = $("#user-dialog");
    $("#add-user-btn").addEventListener("click", () => { $("#user-name").value = ""; udlg.showModal(); $("#user-name").focus(); });
    $("#user-cancel").addEventListener("click", () => udlg.close());
    $("#user-close").addEventListener("click", () => udlg.close());
    $("#user-form").addEventListener("submit", async ev => {
        ev.preventDefault();
        const name = $("#user-name").value.trim();
        if (name && await createUser(name)) udlg.close();
    });
    $("#remove-user-btn").addEventListener("click", async () => {
        if (!state.user || !confirm(`Remove ${state.user} and all of their events and tasks?`)) return;
        try { await api("DELETE", "/users" + qs({ name: state.user })); state.teamSelected.delete(state.user); await refreshAll(); toast("Calendar removed.", "success"); }
        catch (e) { errorToast(e); }
    });
    $("#welcome-form").addEventListener("submit", async ev => {
        ev.preventDefault();
        const name = $("#welcome-name").value.trim();
        if (name) await createUser(name);
    });
    $("#demo-btn").addEventListener("click", loadDemo);

    // tasks, team, activity
    $("#task-form").addEventListener("submit", addTask);
    $("#schedule-btn").addEventListener("click", runSchedule);
    document.querySelectorAll("#strategy button").forEach(b => b.addEventListener("click", async () => {
        state.strategy = b.dataset.strategy;
        try { state.tasks = await api("GET", "/tasks" + qs({ user: state.user, strategy: state.strategy })); renderTasks(); } catch (e) { errorToast(e); }
    }));
    $("#team-form").addEventListener("submit", findTeamSlots);
    $("#clear-activity").addEventListener("click", async () => {
        try { await api("DELETE", "/notifications"); await refreshAll(); } catch (e) { errorToast(e); }
    });

    refreshAll().then(() => { if (state.user && state.teamSelected.size === 0) { state.teamSelected.add(state.user); renderTeamUsers(); } });
    // pick up changes made from other tabs, and keep "today" markers fresh
    setInterval(() => { if (!document.hidden && !dlg.open) refreshAll(); }, 20000);
}

init();
