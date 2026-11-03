#include "core/mask_planner.hpp"

namespace omask::core {

MaskPlan MaskPlanner::plan(const ResultStore &store, int64_t output_ns, const FixedRects &fixed_rects,
			   const TagParams &tag_params, bool screen_model_available, bool tag_model_available,
			   int width, int height)
{
	MaskPlan result;
	result.fixed = decide_fixed(store, output_ns, fixed_params_, screen_model_available);
	result.regions = fixed_regions(result.fixed, fixed_rects, width, height);
	if (result.fixed.result) {
		// PRC-14: リザルトのあいだは、相手のカードの名前の帯を追いかけて隠す
		const std::vector<MaskRegion> bands =
			result_band_regions(store, output_ns, band_params_, width, height);
		result.regions.insert(result.regions.end(), bands.begin(), bands.end());
	}
	if (tag_model_available) {
		const TagDecision decision = decide_tags(store, output_ns, tag_params, memory_);
		result.late = decision.late;
		const std::vector<MaskRegion> tags = tag_regions(decision, tag_params, width, height);
		result.tag_count = static_cast<int>(tags.size());
		result.regions.insert(result.regions.end(), tags.begin(), tags.end());
	}
	return result;
}

} // namespace omask::core
