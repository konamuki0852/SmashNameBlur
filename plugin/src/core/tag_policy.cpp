#include "core/tag_policy.hpp"

#include <algorithm>
#include <cmath>

#include "core/geometry.hpp"

namespace omask::core {

namespace {

bool usable(const ResultStore::Entry &entry)
{
	return entry.done && entry.result.status == AnalysisStatus::kOk;
}

// つなぐ閾値以上の候補 c が、近い時間・近い位置の「見つける閾値」以上の候補とつながるか
bool linked(const ResultStore &store, const ResultStore::Entry &owner, const TagCandidate &candidate,
	    const TagParams &params)
{
	bool found = false;
	const int64_t from = owner.timestamp_ns - params.link_before_ms * kNsPerMs;
	const int64_t to = owner.timestamp_ns + params.link_after_ms * kNsPerMs;
	store.for_each_in(from, to, [&](const ResultStore::Entry &other) {
		if (found || !usable(other))
			return;
		const double dt_ms = std::abs(static_cast<double>(other.timestamp_ns - owner.timestamp_ns)) / kNsPerMs;
		const double grow = params.link_growth_per_frame * dt_ms / kReferenceFrameMs;
		for (const TagCandidate &hit : other.result.tags) {
			if (hit.score < params.find_threshold)
				continue;
			if (std::abs(candidate.x - hit.x) <= params.link_dx + grow &&
			    std::abs(candidate.y - hit.y) <= params.link_dy + grow) {
				found = true;
				return;
			}
		}
	});
	return found;
}

} // namespace

TagDecision decide_tags(const ResultStore &store, int64_t output_ns, const TagParams &params, TagMemory &memory)
{
	TagDecision decision;
	const int64_t hold = params.hold_ms * kNsPerMs;
	const int64_t look = params.look_ahead_ms * kNsPerMs;

	// 手順 1: 保持〜先読みの結果の候補を、二段の閾値で採る
	store.for_each_in(output_ns - hold, output_ns + look, [&](const ResultStore::Entry &entry) {
		if (!usable(entry))
			return;
		for (const TagCandidate &candidate : entry.result.tags) {
			const bool strong = candidate.score >= params.find_threshold;
			const bool weak = !strong && candidate.score >= params.link_threshold;
			if (strong || (weak && linked(store, entry, candidate, params)))
				decision.centers.push_back(PointF{candidate.x, candidate.y});
		}
	});

	// 手順 3: 先読みの範囲に使える結果が無いコマがあれば「間に合わない」。直前の範囲を保持＋先読みまで延長する
	store.for_each_in(output_ns, output_ns + look, [&](const ResultStore::Entry &entry) {
		if (!usable(entry))
			decision.late = true;
	});
	if (!decision.late) {
		memory.centers = decision.centers;
		memory.output_ns = output_ns;
		memory.valid = true;
	} else if (memory.valid && output_ns - memory.output_ns <= hold + look) {
		decision.centers.insert(decision.centers.end(), memory.centers.begin(), memory.centers.end());
	}
	return decision;
}

std::vector<MaskRegion> tag_regions(const TagDecision &decision, const TagParams &params, int width, int height)
{
	std::vector<MaskRegion> regions;
	if (params.half_width <= 0.0f || params.half_height <= 0.0f)
		return regions; // 幅か高さが 0: 頭上タグは隠さない（中心だけの 1 画素がモザイクの 1 ブロックに広がらないように）
	for (const PointF &center : decision.centers) {
		const RectI rect = scale_analysis_box(center, params.half_width, params.half_height, width, height);
		if (rect.empty())
			continue;
		const bool duplicate = std::any_of(regions.begin(), regions.end(),
						   [&](const MaskRegion &region) { return region.rect == rect; });
		if (!duplicate)
			regions.push_back(MaskRegion{rect, RegionKind::kTag});
	}
	return regions;
}

} // namespace omask::core
