#include "core/status.hpp"

namespace omask::core {

void StatusMonitor::on_engine_ready(bool gpu)
{
	base_ = gpu ? EngineState::kRunningGpu : EngineState::kRunningCpu;
	reason_ = UnavailableReason::kNone;
}

void StatusMonitor::on_engine_failed(UnavailableReason reason)
{
	base_ = EngineState::kUnavailable;
	reason_ = reason;
}

void StatusMonitor::on_gpu_fallback()
{
	if (base_ == EngineState::kRunningGpu)
		base_ = EngineState::kRunningCpu;
}

void StatusMonitor::on_late(int64_t now_ns)
{
	late_ns_.push_back(now_ns);
	on_tick(now_ns);
}

void StatusMonitor::on_tick(int64_t now_ns)
{
	while (!late_ns_.empty() && now_ns - late_ns_.front() > kWindowNs)
		late_ns_.pop_front();
	// 5 回以上で「追いついていない」、0 回になったら戻す（行ったり来たりしないように）
	if (late_count() >= kLagThreshold)
		lagging_ = true;
	else if (late_ns_.empty())
		lagging_ = false;
}

void StatusMonitor::on_format(bool supported)
{
	unsupported_ = !supported;
}

EngineState StatusMonitor::state() const
{
	if (unsupported_)
		return EngineState::kUnsupportedFormat;
	const bool running = base_ == EngineState::kRunningGpu || base_ == EngineState::kRunningCpu;
	if (running && lagging_)
		return EngineState::kLagging;
	return base_;
}

bool LogLimiter::allow(const std::string &key, int64_t now_ns)
{
	auto it = last_ns_.find(key);
	if (it != last_ns_.end() && now_ns - it->second < interval_ns_)
		return false;
	last_ns_[key] = now_ns;
	return true;
}

} // namespace omask::core
