// 頭上タグの範囲の決まり（処理詳細設計書 PRC-05）。二段の閾値・先読み・保持・間に合わないときの延長。
#pragma once

#include <cstdint>
#include <vector>

#include "core/params.hpp"
#include "core/result_store.hpp"
#include "core/types.hpp"

namespace omask::core {

// 間に合わないときに延長する、直前に採った候補（PRC-05 手順 3）
struct TagMemory {
	std::vector<PointF> centers;
	int64_t output_ns = 0;
	bool valid = false;
};

struct TagDecision {
	std::vector<PointF> centers; // 採った候補の中心（解析座標）
	bool late = false;           // 先読みの範囲に、受け取り済みなのに使える結果が無いコマがあった
};

// 出力するコマ（時刻 output_ns）について、採る頭上タグの候補を決める。
// 結果の置き場で解析が済んでいる結果だけを使う（因果的。未解析のコマはつなぐ相手にしない）
TagDecision decide_tags(const ResultStore &store, int64_t output_ns, const TagParams &params, TagMemory &memory);

// 採った候補を、コマの座標の隠す範囲にする（同じ矩形は 1 つにまとめる）
std::vector<MaskRegion> tag_regions(const TagDecision &decision, const TagParams &params, int width, int height);

} // namespace omask::core
