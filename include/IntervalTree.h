#pragma once
#include <cstddef>
#include <memory>
#include <vector>
#include "TimeSlot.h"

// Self-balancing (AVL) augmented interval tree (CLRS ch.14).
// Each node stores the max 'end' in its subtree so overlap queries run in O(log N).
class IntervalTree {
public:
    void insert(const TimeSlot& slot, int id);
    bool remove(const TimeSlot& slot, int id);
    bool anyOverlap(const TimeSlot& query) const;              // O(log N)
    std::vector<int> findOverlaps(const TimeSlot& query) const; // O(k + log N)
    std::size_t size() const { return size_; }
    int height() const { return h(root_); }

private:
    struct Node {
        TimeSlot slot;
        int id;
        int maxEnd;
        int height;
        std::unique_ptr<Node> left, right;
        Node(const TimeSlot& s, int i) : slot(s), id(i), maxEnd(s.end()), height(1) {}
    };

    static int h(const std::unique_ptr<Node>& n) { return n ? n->height : 0; }
    static int mx(const std::unique_ptr<Node>& n) { return n ? n->maxEnd : -1; }
    static void update(Node& n);
    static void rotateLeft(std::unique_ptr<Node>& n);
    static void rotateRight(std::unique_ptr<Node>& n);
    static void rebalance(std::unique_ptr<Node>& n);
    static void insertRec(std::unique_ptr<Node>& n, const TimeSlot& s, int id);
    static bool eraseRec(std::unique_ptr<Node>& n, const TimeSlot& s, int id);
    static void collect(const Node* n, const TimeSlot& q, std::vector<int>& out);

    std::unique_ptr<Node> root_;
    std::size_t size_ = 0;
};
