#pragma once
#include <string>

// An unscheduled task waiting in the priority queue.
struct Task {
    int id;
    std::string title;
    std::string owner;
    int duration;   // minutes
    int priority;   // higher = more important
};
