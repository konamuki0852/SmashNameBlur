#include "core/fixed_policy.hpp"

#include "core/geometry.hpp"

namespace omask::core {

FixedActive decide_fixed(const ResultStore &store, int64_t output_ns, const FixedParams &params,
			 bool screen_model_available)
{
	if (!screen_model_available)
		return FixedActive::all();

	FixedActive active;
	const float floor = params.uncertain_floor - kProbabilityEpsilon;
	bool any_result = false;
	bool unsafe = false; // 解析なし・失敗・遅れ、または確率の無い不確か
	const int64_t hold = params.hold_ms * kNsPerMs;
	const int64_t look = params.look_ahead_ms * kNsPerMs;
	store.for_each_in(output_ns - hold, output_ns + look, [&](const ResultStore::Entry &entry) {
		if (!entry.done)
			return;
		any_result = true;
		const AnalysisResult &result = entry.result;
		if (result.status != AnalysisStatus::kOk) {
			unsafe = true;
			return;
		}
		switch (result.screen_kind) {
		case ScreenKind::kVs:
			active.vs = true;
			break;
		case ScreenKind::kBattle:
			active.battle = true;
			break;
		case ScreenKind::kResult:
			active.result = true;
			break;
		case ScreenKind::kUncertain:
			if (!result.has_screen_probs) {
				unsafe = true;
				break;
			}
			active.vs = active.vs || result.screen_probs[1] >= floor;
			active.battle = active.battle || result.screen_probs[2] >= floor;
			active.result = active.result || result.screen_probs[3] >= floor;
			break;
		case ScreenKind::kOther:
			break;
		}
	});
	if (!any_result || unsafe)
		return FixedActive::all();
	return active;
}

std::vector<MaskRegion> fixed_regions(const FixedActive &active, const FixedRects &rects, int width, int height)
{
	std::vector<MaskRegion> regions;
	auto add = [&](const ReferenceRect &rect) {
		const RectI scaled = scale_reference_rect(rect, 0, width, height); // 余白は矩形に含めてある
		if (!scaled.empty())
			regions.push_back(MaskRegion{scaled, RegionKind::kFixed});
	};
	if (active.vs)
		add(rects.vs);
	if (active.battle)
		add(rects.battle);
	if (active.result)
		add(rects.result);
	return regions;
}

} // namespace omask::core
