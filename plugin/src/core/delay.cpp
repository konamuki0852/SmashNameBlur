#include "core/delay.hpp"

#include <algorithm>
#include <vector>

#include "core/params.hpp"

namespace omask::core {

void DelayEstimator::add_timestamp(int64_t timestamp_ns)
{
	// 時刻の飛び（フィルターの無効→有効など）の間隔を入れると、遅延が何秒にもなる
	if (has_last_ && timestamp_ns > last_ns_ && timestamp_ns - last_ns_ <= kMaxIntervalNs) {
		intervals_.push_back(timestamp_ns - last_ns_);
		if (intervals_.size() > static_cast<size_t>(kWindow))
			intervals_.pop_front();
	}
	has_last_ = true;
	last_ns_ = timestamp_ns;
}

int64_t DelayEstimator::frame_interval_ns() const
{
	if (intervals_.empty())
		return kDefaultIntervalNs;
	std::vector<int64_t> sorted(intervals_.begin(), intervals_.end());
	auto middle = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2);
	std::nth_element(sorted.begin(), middle, sorted.end());
	return *middle;
}

int64_t DelayEstimator::delay_ns(int look_ahead_ms, int margin_frames) const
{
	return static_cast<int64_t>(look_ahead_ms) * kNsPerMs + margin_frames * frame_interval_ns();
}

void DelayEstimator::reset()
{
	intervals_.clear();
	has_last_ = false;
	last_ns_ = 0;
}

} // namespace omask::core
