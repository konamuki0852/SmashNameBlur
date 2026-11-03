#include "core/timing.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace omask::core {

void TimingWindow::add(double ms)
{
	if (samples_.size() < kMaxSamples)
		samples_.push_back(ms);
}

TimingSummary TimingWindow::take()
{
	TimingSummary summary;
	summary.count = samples_.size();
	if (samples_.empty())
		return summary;
	summary.average_ms =
		std::accumulate(samples_.begin(), samples_.end(), 0.0) / static_cast<double>(samples_.size());
	std::sort(samples_.begin(), samples_.end());
	const auto rank = static_cast<std::size_t>(std::ceil(0.99 * static_cast<double>(samples_.size())));
	summary.p99_ms = samples_[std::min(samples_.size(), std::max<std::size_t>(rank, 1)) - 1];
	summary.max_ms = samples_.back();
	samples_.clear();
	return summary;
}

} // namespace omask::core
