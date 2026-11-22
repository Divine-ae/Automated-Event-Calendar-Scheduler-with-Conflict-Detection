#pragma once
#include <string>
#include "TimeSlot.h"

// A scheduled event on a user's calendar. Fields are private (encapsulation).
class Event {
public:
    Event(int id, std::string title, std::string owner, TimeSlot slot, int priority = 1);

    int id() const { return id_; }
    const std::string& title() const { return title_; }
    const std::string& owner() const { return owner_; }
    const TimeSlot& slot() const { return slot_; }
    int priority() const { return priority_; }  // higher = more important

    void setTitle(const std::string& t) { title_ = t; }
    void setSlot(const TimeSlot& s) { slot_ = s; }
    void setPriority(int p) { priority_ = p; }

private:
    int id_;
    std::string title_;
    std::string owner_;
    TimeSlot slot_;
    int priority_;
};
