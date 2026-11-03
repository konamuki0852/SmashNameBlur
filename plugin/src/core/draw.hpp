// モザイク・塗りつぶし・確認用の枠の描き込み（処理詳細設計書 PRC-06）。コマの画素をその場で書き換える。
#pragma once

#include <cstdint>
#include <vector>

#include "core/frame.hpp"
#include "core/types.hpp"

namespace omask::core {

// 隠し方の種類（ENT-01 style）
enum class StyleKind { kMosaic, kFill };

// 見た目の設定（ENT-01 の style・mosaic_strength・fill_color・debug_overlay）
struct DrawStyle {
	StyleKind kind = StyleKind::kMosaic;
	int mosaic_block_px = 16;          // モザイクの 1 マスの大きさ（1080p 換算。強さ 8・16・36）
	uint32_t fill_color = 0xFF000000u; // 塗りつぶしの色（0xAABBGGRR。不透明に限る）
	bool debug_overlay = false;        // 確認用の枠を描く
};

// モザイクの塊の大きさ（画素）。粗さ × コマの幅 ÷ 1920。4 未満にしない。色差が間引かれた形式では偶数にする
int mosaic_block_size(int frame_width, int mosaic_block_px, bool chroma_subsampled);

// 範囲の座標を偶数に広げる（色差の画素の境界に合わせる。PRC-06 手順 1）。コマの内側に切り詰める
RectI align_even(const RectI &rect, int width, int height);

// 範囲にモザイク（または塗りつぶし）を描き、確認用の表示なら外側に枠を描く。対応外の形式なら何もしない。
// 塊の境界はコマ全体の格子にそろえ、塊の中の元の画素の平均で、範囲の内側だけを塗る
void draw_regions(FrameView &frame, const std::vector<MaskRegion> &regions, const DrawStyle &style);

} // namespace omask::core
