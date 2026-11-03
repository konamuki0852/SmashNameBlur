// 座標の換算と切り詰め（処理詳細設計書 PRC-03 手順 2・PRC-05 手順 2）
#pragma once

#include "core/params.hpp"
#include "core/types.hpp"

namespace omask::core {

// 矩形をコマ（0..width, 0..height）の内側に切り詰める。外に出ていれば空になる
RectI clip_rect(const RectI &rect, int width, int height);

// 1080p 換算の矩形に余白を足し、コマの大きさに比例させる。外側へ丸め（覆い漏れを作らない）、切り詰める
RectI scale_reference_rect(const ReferenceRect &rect, int margin, int width, int height);

// 解析座標の中心と半分の大きさの矩形を、コマの座標に直す。外側へ丸め、切り詰める
RectI scale_analysis_box(PointF center, float half_width, float half_height, int width, int height);

} // namespace omask::core
