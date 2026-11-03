// 位置の決まった範囲（処理詳細設計書 PRC-03）。画面の種類の結果から、3 つの範囲のどれを隠すかを決める。
#pragma once

#include <cstdint>
#include <vector>

#include "core/params.hpp"
#include "core/result_store.hpp"
#include "core/types.hpp"

namespace omask::core {

struct FixedActive {
	bool vs = false;
	bool battle = false;
	bool result = false;

	static FixedActive all() { return FixedActive{true, true, true}; }
	bool operator==(const FixedActive &other) const
	{
		return vs == other.vs && battle == other.battle && result == other.result;
	}
	bool operator!=(const FixedActive &other) const { return !(*this == other); }
};

// 出力するコマ（時刻 output_ns）で有効にする範囲を決める。[output_ns − 保持, output_ns + 先読み] の解析が済んだ結果を見る。
// screennet を使えない・結果が 1 つも無い・解析なし／失敗のコマがある、のどれかなら全部を有効にする（安全側）
FixedActive decide_fixed(const ResultStore &store, int64_t output_ns, const FixedParams &params,
			 bool screen_model_available);

// 有効な範囲を、コマの座標の隠す範囲にする。矩形は既定（default_fixed_rects。battle は決まりで変わる）か利用者の値
std::vector<MaskRegion> fixed_regions(const FixedActive &active, const FixedRects &rects, int width, int height);

} // namespace omask::core
