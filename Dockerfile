
FROM gcc:14

WORKDIR /app

COPY include/ ./include/
COPY src/ ./src/
COPY frontend/ ./frontend/
COPY third_party/ ./third_party/

RUN g++ -std=c++17 -O2 -Wall -Wextra -pthread \
    -Iinclude -Ithird_party \
    src/CalendarService.cpp \
    src/ConflictDetector.cpp \
    src/Event.cpp \
    src/IntervalTree.cpp \
    src/Observer.cpp \
    src/TaskScheduler.cpp \
    src/TimeSlot.cpp \
    src/UserCalendar.cpp \
    src/server.cpp \
    -o chronosync_server

ENV PORT=10000

EXPOSE 10000

CMD ["sh", "-c", "./chronosync_server --host 0.0.0.0 --port ${PORT:-10000} --data /tmp/chronosync.json --frontend /app/frontend"]

