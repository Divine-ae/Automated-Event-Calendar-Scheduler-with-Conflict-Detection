
const form = document.getElementById("event-form");

const titleInput = document.getElementById("event-title");
const dateInput = document.getElementById("event-date");
const startInput = document.getElementById("event-start");
const endInput = document.getElementById("event-end");

// Load saved events from this browser.
let events = JSON.parse(localStorage.getItem("chronosync-events") || "[]");
let editingEventId = null;
function saveEvents() {
    localStorage.setItem("chronosync-events", JSON.stringify(events));
}

// Convert "09:30" into minutes after midnight.
function toMinutes(time) {
    const [hours, minutes] = time.split(":").map(Number);
    return hours * 60 + minutes;
}

// Two events conflict if they overlap on the same date.
function findConflict(newEvent) {
    return events.find(event =>
        event.date === newEvent.date &&
        toMinutes(newEvent.start) < toMinutes(event.end) &&
        toMinutes(newEvent.end) > toMinutes(event.start)
    );
}

function formatTime(time) {
    const [hours, minutes] = time.split(":").map(Number);
    const suffix = hours >= 12 ? "PM" : "AM";
    const displayHour = hours % 12 || 12;

    return `${displayHour}:${String(minutes).padStart(2, "0")} ${suffix}`;
}

function renderEvents() {
    const eventSection = document.querySelector("#events");
    const heading = eventSection.querySelector(".panel-heading");

    // Remove the example events, keeping the heading.
    eventSection.querySelectorAll(".event-row").forEach(row => row.remove());

    const sortedEvents = [...events].sort((a, b) =>
        (a.date + a.start).localeCompare(b.date + b.start)
    );

    sortedEvents.forEach(event => {
        const row = document.createElement("div");
        row.className = "event-row";

        const date = new Date(event.date + "T12:00:00");
        const day = String(date.getDate()).padStart(2, "0");
        const month = date.toLocaleString("en", { month: "short" }).toUpperCase();

        const dateBox = document.createElement("div");
        dateBox.className = "event-date";

        const dayText = document.createElement("b");
        dayText.textContent = day;

        const monthText = document.createElement("span");
        monthText.textContent = month;

        dateBox.append(dayText, monthText);

        const details = document.createElement("div");

        const title = document.createElement("strong");
        title.textContent = event.title;

        const time = document.createElement("p");
        time.textContent =
            `${formatTime(event.start)} – ${formatTime(event.end)}`;

        details.append(title, time);

        const dot = document.createElement("span");
        dot.className = "event-dot blue-dot";

        
        // Create an Edit button for this event.
        const editBtn = document.createElement("button");
        editBtn.textContent = "Edit";
        editBtn.className = "edit-btn";

        editBtn.addEventListener("click", function () {
            editingEventId = event.id;

            titleInput.value = event.title;
            dateInput.value = event.date;
            startInput.value = event.start;
            endInput.value = event.end;

            form.querySelector('button[type="submit"]').textContent =
                "Update Event";

            form.scrollIntoView({ behavior: "smooth", block: "center" });
        });

        // Create a delete button for this event.
        const deleteBtn = document.createElement("button");
        deleteBtn.textContent = "Delete";
        deleteBtn.className = "delete-btn";


        deleteBtn.addEventListener("click", function () {
            const confirmDelete = confirm(
                `Are you sure you want to delete "${event.title}"?`
            );

            if (!confirmDelete) return;

            events = events.filter(item => item.id !== event.id);

            saveEvents();
            renderEvents();
        });

        row.append(dateBox, details, dot, editBtn, deleteBtn);
        eventSection.appendChild(row);

    });

    // Update the dashboard statistics.
    const stats = document.querySelectorAll(".stat-card h2");
    if (stats.length >= 2) {
        stats[0].textContent = events.length;
        const today = new Date();
        const todayString =
            `${today.getFullYear()}-${String(today.getMonth() + 1).padStart(2, "0")}-${String(today.getDate()).padStart(2, "0")}`;

        const tomorrow = new Date(today);
        tomorrow.setDate(tomorrow.getDate() + 1);
        const tomorrowString =
            `${tomorrow.getFullYear()}-${String(tomorrow.getMonth() + 1).padStart(2, "0")}-${String(tomorrow.getDate()).padStart(2, "0")}`;

        const upcoming = events.filter(event =>
            event.date >= todayString && event.date <= tomorrowString
        );

        stats[1].textContent = upcoming.length;
    }

    updateConflictMonitor();
}

function updateConflictMonitor(message = "") {
    const panel = document.querySelector("#conflicts");
    const badge = panel.querySelector(".warning-badge");
    const alert = panel.querySelector(".alert-card");

    // Detect any existing overlapping events.
    let conflicts = [];

    for (let i = 0; i < events.length; i++) {
        for (let j = i + 1; j < events.length; j++) {
            if (
                events[i].date === events[j].date &&
                toMinutes(events[i].start) < toMinutes(events[j].end) &&
                toMinutes(events[i].end) > toMinutes(events[j].start)
            ) {
                conflicts.push([events[i], events[j]]);
            }
        }
    }

    badge.textContent = `${conflicts.length} Alert${conflicts.length === 1 ? "" : "s"}`;

    const strong = alert.querySelector("strong");
    const paragraph = alert.querySelector("p");
    const small = alert.querySelector("small");

    strong.textContent = message || (
        conflicts.length ? "Overlapping events detected" : "No scheduling conflicts"
    );

    paragraph.textContent = conflicts.length
        ? conflicts.map(pair => `${pair[0].title} overlaps ${pair[1].title}`).join("; ")
        : "Your saved events have no overlapping time slots.";

    small.textContent = "Browser demo — C++ backend not connected yet";
}
    
form.addEventListener("submit", function (event) {
    event.preventDefault();

    const newEvent = {
        id: editingEventId ?? Date.now(),
        title: titleInput.value.trim(),
        date: dateInput.value,
        start: startInput.value,
        end: endInput.value
    };

    if (!newEvent.title || !newEvent.date ||
        !newEvent.start || !newEvent.end) {
        alert("Please fill in all event details.");
        return;
    }

    if (toMinutes(newEvent.end) <= toMinutes(newEvent.start)) {
        alert("End time must be later than start time.");
        return;
    }

    // Exclude the event being edited from conflict checking.
    const conflict = events.find(event =>
        event.id !== newEvent.id &&
        event.date === newEvent.date &&
        toMinutes(newEvent.start) < toMinutes(event.end) &&
        toMinutes(newEvent.end) > toMinutes(event.start)
    );

    if (conflict) {
        alert(
            `Scheduling conflict! "${newEvent.title}" overlaps with ` +
            `"${conflict.title}" on the same date.`
        );
        return;
    }

    if (editingEventId !== null) {
        // Update the existing event.
        events = events.map(event =>
            event.id === editingEventId ? newEvent : event
        );

        alert("Event updated successfully!");
    } else {
        // Create a new event.
        events.push(newEvent);
        alert("Event created successfully!");
    }

    saveEvents();
    renderEvents();
    form.reset();

    editingEventId = null;

    const submitBtn = form.querySelector('button[type="submit"]');
    if (submitBtn) submitBtn.textContent = "Create Event";
});


// Show previously saved events when the page opens.
renderEvents();
