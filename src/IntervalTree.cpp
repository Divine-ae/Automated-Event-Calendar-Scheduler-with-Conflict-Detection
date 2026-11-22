#include "IntervalTree.h"
#include <algorithm>

void IntervalTree::update(Node& n) {
    n.height = 1 + std::max(h(n.left), h(n.right));
    n.maxEnd = std::max({n.slot.end(), mx(n.left), mx(n.right)});
}

void IntervalTree::rotateRight(std::unique_ptr<Node>& y) {
    auto x = std::move(y->left);
    y->left = std::move(x->right);
    update(*y);
    x->right = std::move(y);
    update(*x);
    y = std::move(x);
}

void IntervalTree::rotateLeft(std::unique_ptr<Node>& x) {
    auto y = std::move(x->right);
    x->right = std::move(y->left);
    update(*x);
    y->left = std::move(x);
    update(*y);
    x = std::move(y);
}

void IntervalTree::rebalance(std::unique_ptr<Node>& n) {
    update(*n);
    int bf = h(n->left) - h(n->right);
    if (bf > 1) {
        if (h(n->left->left) < h(n->left->right)) rotateLeft(n->left);
        rotateRight(n);
    } else if (bf < -1) {
        if (h(n->right->right) < h(n->right->left)) rotateRight(n->right);
        rotateLeft(n);
    }
}

static bool keyLess(const TimeSlot& a, int ida, const TimeSlot& b, int idb) {
    return a.start() != b.start() ? a.start() < b.start() : ida < idb;
}

void IntervalTree::insertRec(std::unique_ptr<Node>& n, const TimeSlot& s, int id) {
    if (!n) { n = std::make_unique<Node>(s, id); return; }
    if (keyLess(s, id, n->slot, n->id)) insertRec(n->left, s, id);
    else insertRec(n->right, s, id);
    rebalance(n);
}

bool IntervalTree::eraseRec(std::unique_ptr<Node>& n, const TimeSlot& s, int id) {
    if (!n) return false;
    bool removed;
    if (n->id == id && n->slot == s) {
        if (!n->left || !n->right) {
            std::unique_ptr<Node> child = std::move(n->left ? n->left : n->right);
            n = std::move(child);
            return true;
        }
        const Node* m = n->right.get();
        while (m->left) m = m->left.get();
        TimeSlot ms = m->slot;
        int mid = m->id;
        n->slot = ms;
        n->id = mid;
        removed = eraseRec(n->right, ms, mid);
    } else if (keyLess(s, id, n->slot, n->id)) {
        removed = eraseRec(n->left, s, id);
    } else {
        removed = eraseRec(n->right, s, id);
    }
    if (removed && n) rebalance(n);
    return removed;
}

void IntervalTree::insert(const TimeSlot& slot, int id) {
    insertRec(root_, slot, id);
    ++size_;
}

bool IntervalTree::remove(const TimeSlot& slot, int id) {
    bool ok = eraseRec(root_, slot, id);
    if (ok) --size_;
    return ok;
}

bool IntervalTree::anyOverlap(const TimeSlot& q) const {
    const Node* n = root_.get();
    while (n) {
        if (n->slot.overlaps(q)) return true;
        if (n->left && n->left->maxEnd > q.start()) n = n->left.get();
        else n = n->right.get();
    }
    return false;
}

void IntervalTree::collect(const Node* n, const TimeSlot& q, std::vector<int>& out) {
    if (!n) return;
    if (n->left && n->left->maxEnd > q.start()) collect(n->left.get(), q, out);
    if (n->slot.overlaps(q)) out.push_back(n->id);
    if (n->slot.start() < q.end()) collect(n->right.get(), q, out);
}

std::vector<int> IntervalTree::findOverlaps(const TimeSlot& q) const {
    std::vector<int> out;
    collect(root_.get(), q, out);
    return out;
}
