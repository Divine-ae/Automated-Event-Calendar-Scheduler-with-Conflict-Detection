#include "Event.h"
#include <utility>

Event::Event(int id, std::string title, std::string owner, TimeSlot slot, int priority)
    : id_(id), title_(std::move(title)), owner_(std::move(owner)),
      slot_(slot), priority_(priority) {}
