// テスト用の小さな道具（合成の解析結果を作る）
#pragma once

#include <cstdint>
#include <vector>

#include "core/result_store.hpp"
#include "core/types.hpp"

namespace omask::test {

constexpr int64_t ms(int64_t value)
{
	return value * 1000000;
}

inline core::AnalysisResult make_result(int64_t timestamp_ns, core::ScreenKind kind = core::ScreenKind::kBattle,
					std::vector<core::TagCandidate> tags = {})
{
	core::AnalysisResult result;
	result.timestamp_ns = timestamp_ns;
	result.status = core::AnalysisStatus::kOk;
	result.screen_kind = kind;
	result.has_screen_probs = true;
	if (kind != core::ScreenKind::kUncertain)
		result.screen_probs[static_cast<int>(kind)] = 1.0f;
	result.tags = std::move(tags);
	return result;
}

// 解析が済んだ結果を置く
inline void put_done(core::ResultStore &store, const core::AnalysisResult &result)
{
	store.add_frame(result.timestamp_ns);
	store.put(result);
}

// [from_ms, to_ms] に step_ms おきに、候補の無い解析済みの結果を置く
inline void fill_done(core::ResultStore &store, int64_t from_ms, int64_t to_ms, int64_t step_ms,
		      core::ScreenKind kind = core::ScreenKind::kBattle)
{
	for (int64_t t = from_ms; t <= to_ms; t += step_ms)
		put_done(store, make_result(ms(t), kind));
}

inline bool contains(const std::vector<core::PointF> &points, float x, float y)
{
	for (const core::PointF &p : points) {
		if (p.x == x && p.y == y)
			return true;
	}
	return false;
}

} // namespace omask::test
