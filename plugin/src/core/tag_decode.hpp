// 頭上タグの検出器の出力から候補を取り出す（処理詳細設計書 PRC-04 手順 4・5、データ設計書 ENT-04）
#pragma once

#include <vector>

#include "core/params.hpp"
#include "core/types.hpp"

namespace omask::core {

constexpr int kTagGridWidth = kAnalysisWidth / kGridStride;   // 80
constexpr int kTagGridHeight = kAnalysisHeight / kGridStride; // 45

// out は [3, 45, 80]（0 枚目: 中心らしさのロジット、1・2 枚目: 格子の中の横・縦のずれ）。
// 3x3 の近傍で最大（同じ値は両方）かつ確からしさ（シグモイド）が threshold 以上の格子を候補にする。
// 位置 =（格子の番号 ＋ ずれ）× 8（解析座標）
std::vector<TagCandidate> decode_tag_candidates(const float *out, float threshold);

} // namespace omask::core
