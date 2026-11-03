// 出力するコマの隠す範囲を決める（処理詳細設計書 PRC-01 手順 8 = PRC-03 ＋ PRC-05）。
// プラグインと検証用 CLI（omask-eval）で同じものを使い、因果的に決める。
#pragma once

#include <cstdint>
#include <vector>

#include "core/fixed_policy.hpp"
#include "core/params.hpp"
#include "core/result_band.hpp"
#include "core/result_store.hpp"
#include "core/tag_policy.hpp"
#include "core/types.hpp"

namespace omask::core {

struct MaskPlan {
	std::vector<MaskRegion> regions; // 位置の決まった範囲のあとに頭上タグの範囲
	FixedActive fixed;
	int tag_count = 0;
	bool late = false; // 間に合わなかった（PRC-09 で数える）
};

class MaskPlanner {
public:
	explicit MaskPlanner(const FixedParams &fixed_params = FixedParams{}) : fixed_params_(fixed_params) {}

	// tag_params: 頭上タグの決まり（組に利用者の調整を当てたもの）。screen_model_available: screennet を使えるか。
	// tag_model_available: tagnet を使えるか（使えなければ頭上タグは隠せない）。fixed_rects: 位置の決まった範囲の矩形
	MaskPlan plan(const ResultStore &store, int64_t output_ns, const FixedRects &fixed_rects,
		      const TagParams &tag_params, bool screen_model_available, bool tag_model_available, int width,
		      int height);
	void reset() { memory_ = TagMemory{}; }

private:
	FixedParams fixed_params_;
	ResultBandParams band_params_;
	TagMemory memory_;
};

} // namespace omask::core
