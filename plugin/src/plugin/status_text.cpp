#include "plugin/status_text.hpp"

#include <obs-module.h>

#include <cmath>

#include "core/params.hpp"
#include "core/text.hpp"

namespace omask::plugin {

namespace {

const char *reason_key(core::UnavailableReason reason)
{
	switch (reason) {
	case core::UnavailableReason::kRuntimeMissing:
		return "Reason.RuntimeMissing";
	case core::UnavailableReason::kRuntimeVersion:
		return "Reason.RuntimeVersion";
	case core::UnavailableReason::kModelMissing:
		return "Reason.ModelMissing";
	case core::UnavailableReason::kModelFormat:
		return "Reason.ModelFormat";
	case core::UnavailableReason::kInferenceFailed:
	case core::UnavailableReason::kNone:
		break;
	}
	return "Reason.InferenceFailed";
}

int delay_ms(int64_t delay_ns)
{
	return static_cast<int>(std::lround(static_cast<double>(delay_ns) / core::kNsPerMs));
}

} // namespace

StatusText status_text(core::EngineState state, core::UnavailableReason reason, int late_count,
		       const std::string &format)
{
	switch (state) {
	case core::EngineState::kPreparing:
		return {obs_module_text("Status.Preparing"), OBS_TEXT_INFO_NORMAL};
	case core::EngineState::kRunningGpu:
		return {obs_module_text("Status.RunningGpu"), OBS_TEXT_INFO_NORMAL};
	case core::EngineState::kRunningCpu:
		return {obs_module_text("Status.RunningCpu"), OBS_TEXT_INFO_WARNING};
	case core::EngineState::kLagging:
		return {core::fill_placeholder(obs_module_text("Status.Lagging"), "count", std::to_string(late_count)),
			OBS_TEXT_INFO_WARNING};
	case core::EngineState::kUnavailable:
		return {core::fill_placeholder(obs_module_text("Status.Unavailable"), "reason",
					       obs_module_text(reason_key(reason))),
			OBS_TEXT_INFO_ERROR};
	case core::EngineState::kUnsupportedFormat:
		return {core::fill_placeholder(obs_module_text("Status.UnsupportedFormat"), "format", format),
			OBS_TEXT_INFO_ERROR};
	}
	return {};
}

std::string delay_text(int64_t delay_ns, int64_t interval_ns)
{
	const long fps = interval_ns > 0 ? std::lround(1e9 / static_cast<double>(interval_ns)) : 0;
	std::string text =
		core::fill_placeholder(obs_module_text("Delay.Value"), "delay", std::to_string(delay_ms(delay_ns)));
	return core::fill_placeholder(text, "fps", std::to_string(fps));
}

const char *state_name(core::EngineState state)
{
	switch (state) {
	case core::EngineState::kPreparing:
		return "preparing";
	case core::EngineState::kRunningGpu:
		return "running_gpu";
	case core::EngineState::kRunningCpu:
		return "running_cpu";
	case core::EngineState::kLagging:
		return "lagging";
	case core::EngineState::kUnavailable:
		return "unavailable";
	case core::EngineState::kUnsupportedFormat:
		return "unsupported_format";
	}
	return "unknown";
}

const char *reason_name(core::UnavailableReason reason)
{
	switch (reason) {
	case core::UnavailableReason::kNone:
		return "none";
	case core::UnavailableReason::kRuntimeMissing:
		return "runtime_missing";
	case core::UnavailableReason::kRuntimeVersion:
		return "runtime_version";
	case core::UnavailableReason::kModelMissing:
		return "model_missing";
	case core::UnavailableReason::kModelFormat:
		return "model_format";
	case core::UnavailableReason::kInferenceFailed:
		return "inference_failed";
	}
	return "unknown";
}

} // namespace omask::plugin
