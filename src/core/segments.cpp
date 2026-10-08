#include "core/segments.h"

#include <algorithm>

namespace dm {

SegmentPlanner::SegmentPlanner(int64_t totalSize, int64_t minSplitSize)
    : total_(totalSize), baseMinSplit_(std::max<int64_t>(minSplitSize, 1)), minSplit_(baseMinSplit_) {}

void SegmentPlanner::setMinSplit(int64_t bytes) {
    std::lock_guard lock(mutex_);
    minSplit_ = std::max(bytes, baseMinSplit_);
}

void SegmentPlanner::restore(std::vector<Segment> segments) {
    std::lock_guard lock(mutex_);
    segments_ = std::move(segments);
    for (auto& segment : segments_) {
        segment.reserved = segment.written;
        segment.owned = false;
    }
}

size_t SegmentPlanner::acquireFirst() {
    std::lock_guard lock(mutex_);
    segments_.assign(1, Segment{0, total_, 0, 0, true});
    return 0;
}

std::optional<size_t> SegmentPlanner::acquire() {
    std::lock_guard lock(mutex_);

    for (size_t i = 0; i < segments_.size(); ++i) {
        Segment& segment = segments_[i];
        if (!segment.owned && segment.remaining() > 0) {
            segment.owned = true;
            return i;
        }
    }

    size_t largest = segments_.size();
    for (size_t i = 0; i < segments_.size(); ++i) {
        const Segment& segment = segments_[i];
        if (segment.owned &&
            (largest == segments_.size() || segment.remaining() > segments_[largest].remaining())) {
            largest = i;
        }
    }
    if (largest == segments_.size() || segments_[largest].remaining() < 2 * minSplit_) {
        return std::nullopt;
    }

    Segment& victim = segments_[largest];
    const int64_t middle = victim.reserved + victim.remaining() / 2;
    const Segment tail{middle, victim.end, middle, middle, true};
    victim.end = middle;
    segments_.push_back(tail);
    return segments_.size() - 1;
}

SegmentPlanner::Claim SegmentPlanner::reserve(size_t index, int64_t wanted) {
    std::lock_guard lock(mutex_);
    Segment& segment = segments_[index];
    const int64_t length = std::clamp<int64_t>(segment.end - segment.reserved, 0, wanted);
    const Claim claim{segment.reserved, length};
    segment.reserved += length;
    return claim;
}

void SegmentPlanner::commit(size_t index, int64_t length) {
    std::lock_guard lock(mutex_);
    segments_[index].written += length;
}

void SegmentPlanner::release(size_t index) {
    std::lock_guard lock(mutex_);
    Segment& segment = segments_[index];
    segment.reserved = segment.written;
    segment.owned = false;
}

bool SegmentPlanner::reachedEnd(size_t index) const {
    std::lock_guard lock(mutex_);
    return segments_[index].reserved >= segments_[index].end;
}

int64_t SegmentPlanner::position(size_t index) const {
    std::lock_guard lock(mutex_);
    return segments_[index].reserved;
}

int64_t SegmentPlanner::end(size_t index) const {
    std::lock_guard lock(mutex_);
    return segments_[index].end;
}

bool SegmentPlanner::allComplete() const {
    std::lock_guard lock(mutex_);
    return std::all_of(segments_.begin(), segments_.end(),
                       [](const Segment& segment) { return segment.complete(); });
}

int64_t SegmentPlanner::bytesWritten() const {
    std::lock_guard lock(mutex_);
    int64_t total = 0;
    for (const auto& segment : segments_) total += segment.written - segment.start;
    return total;
}

std::vector<Segment> SegmentPlanner::snapshot() const {
    std::lock_guard lock(mutex_);
    return segments_;
}

}  // namespace dm
