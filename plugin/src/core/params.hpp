// 決まりのパラメータと位置の決まった範囲（データ設計書 ENT-02・03）。時間はミリ秒で持ち、使うときに換算する。
#pragma once

#include <algorithm>
#include <cstdint>

#include "core/types.hpp"

namespace omask::core {

constexpr int kAnalysisWidth = 640; // 解析座標（ENT-04・05 の入力の大きさ）
constexpr int kAnalysisHeight = 360;
constexpr int kGridStride = 8;        // tagnet の出力の格子（解析座標）
constexpr int kReferenceWidth = 1920; // 位置の決まった範囲の座標の基準（1080p）
constexpr int kReferenceHeight = 1080;
constexpr int64_t kNsPerMs = 1000000;
constexpr double kReferenceFrameMs = 1000.0 / 60.0; // つなぐ距離を広げる時間の単位（60fps の 1 コマ）
constexpr float kProbabilityEpsilon =
	1e-6f; // 確率と基準の比較の余裕（ソフトマックスの丸めで「ちょうど」が外れないように）

constexpr int kAnalysisMarginFrames = 2; // 解析の余裕（ENT-03。遅延 D = 先読み ＋ この数 × コマの間隔。PRC-01）

// 頭上タグの範囲の決まり（ENT-03）。距離・大きさは解析座標
struct TagParams {
	float find_threshold = 0.5f;        // これ以上はそれだけで頭上タグとみなす
	float link_threshold = 0.3f;        // これ以上・見つける閾値未満は、近くの見つけたタグとつながれば採る
	int link_before_ms = 1000;          // つなぐ相手を探す時間（前）
	int link_after_ms = 100;            // つなぐ相手を探す時間（後）
	float link_dx = 40.0f;              // つなぐ距離（横）
	float link_dy = 20.0f;              // つなぐ距離（縦）
	float link_growth_per_frame = 4.0f; // 時間差 1 コマ（16.7 ms）ごとに広げる距離
	int look_ahead_ms = 100;            // 後のコマで見つかったタグを前のコマにも当てる時間
	int hold_ms = 500;                  // 見つかったタグの範囲を隠し続ける時間
	float half_width = 55.0f;           // 隠す範囲の半幅（1080p で 330 px）
	float half_height = 16.0f;          // 隠す範囲の半高（1080p で 96 px）
};

// 決まりの組を選ぶ（ENT-01 policy → ENT-03）。違うのは保持だけ
inline TagParams tag_params_for(Policy policy)
{
	TagParams params;
	params.hold_ms = policy == Policy::kFewerMosaic ? 200 : 500;
	return params;
}

// 利用者が変えられる頭上タグの範囲の大きさ・保持・先読み（ENT-01 tag_*・SCR-01-I13〜I15・I17。1080p 換算の画素とミリ秒）。
// 既定は漏れ最優先の組と同じ。幅か高さが 0 なら頭上タグは隠さない（利用者が見比べるための下限。ラベルは約 186×42 px）
constexpr int kTagWidthMinPx = 0;
constexpr int kTagWidthMaxPx = 600;
constexpr int kTagWidthDefaultPx = 160;
constexpr int kTagHeightMinPx = 0;
constexpr int kTagHeightMaxPx = 200;
constexpr int kTagHeightDefaultPx = 48;
constexpr int kTagHoldMinMs = 0;
constexpr int kTagHoldMaxMs = 1000;
constexpr int kTagHoldDefaultMs = 32;
constexpr int kTagLookAheadMinMs = 0;
constexpr int kTagLookAheadMaxMs = 100; // 遅延 D の先読みより長くしない（映像の遅れを増やさない。delay_look_ahead_ms）
constexpr int kTagLookAheadDefaultMs = 32;

struct TagAdjust {
	int width_px = kTagWidthDefaultPx;
	int height_px = kTagHeightDefaultPx;
	int hold_ms = kTagHoldDefaultMs;
	int look_ahead_ms = kTagLookAheadDefaultMs;
};

inline bool operator==(const TagAdjust &a, const TagAdjust &b)
{
	return a.width_px == b.width_px && a.height_px == b.height_px && a.hold_ms == b.hold_ms &&
	       a.look_ahead_ms == b.look_ahead_ms;
}

inline bool operator!=(const TagAdjust &a, const TagAdjust &b)
{
	return !(a == b);
}

// 決まりの組の隠す範囲の大きさ・保持・先読みを、利用者の調整で置き換える（PRC-05 パラメータの組み立て）
inline TagParams apply_tag_adjust(TagParams params, const TagAdjust &adjust)
{
	params.half_width = static_cast<float>(adjust.width_px) * kAnalysisWidth / kReferenceWidth / 2.0f;
	params.half_height = static_cast<float>(adjust.height_px) * kAnalysisHeight / kReferenceHeight / 2.0f;
	params.hold_ms = adjust.hold_ms;
	params.look_ahead_ms = adjust.look_ahead_ms;
	return params;
}

// 位置の決まった範囲と画面判定のパラメータ（ENT-03。決まりの組によらず共通）
struct FixedParams {
	int hold_ms = 500;               // 画面が切り替わったあとも隠し続ける時間
	int look_ahead_ms = 100;         // 画面が切り替わる前から隠し始める時間
	float uncertain_floor = 0.2f;    // 不確かなとき、この確率以上の種類の範囲も有効にする
	float decision_threshold = 0.8f; // 最大の確率がこれ未満なら不確か（PRC-02）
	float bright_threshold = 0.85f;  // 明るさがこれを超えるコマは白いコマの決まりの対象（PRC-02）
	int bright_gate_ms = 3000;       // この間にバトル・リザルトの判定があれば白いコマの決まりを使わない
};

// 遅延 D に使う先読みの長さ（頭上タグと位置の決まった範囲の長いほう）。利用者の頭上タグの先読みは
// kTagLookAheadMaxMs 以下なので、利用者の値によらずこの長さで足りる（遅延バッファを作り直さない）
inline int delay_look_ahead_ms()
{
	return std::max(TagParams{}.look_ahead_ms, FixedParams{}.look_ahead_ms);
}

// 結果の置き場に残す長さ（出力するコマからさかのぼる時間）。頭上タグの保持の最大（利用者が変えられる上限）と、
// つなぐ相手を探す時間の和に、余裕を足す（PRC-01）
inline int result_keep_ms()
{
	const int hold = std::max(kTagHoldMaxMs, tag_params_for(Policy::kLeakFirst).hold_ms);
	return std::max(hold + TagParams{}.link_before_ms, FixedParams{}.hold_ms) + 100;
}

// 1080p 換算の矩形（左上を含み右下を含まない）
struct ReferenceRect {
	int x0;
	int y0;
	int x1;
	int y1;
};

// 位置の決まった範囲の実際の矩形（1080p 換算。余白込み）。利用者が位置と大きさを変えられる
// （ENT-01 fixed_*。SCR-01-I19〜I36）。幅か高さが 0 の範囲は隠さない
struct FixedRects {
	ReferenceRect vs;
	ReferenceRect battle;
	ReferenceRect result;
};

inline bool operator==(const ReferenceRect &a, const ReferenceRect &b)
{
	return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

inline bool operator==(const FixedRects &a, const FixedRects &b)
{
	return a.vs == b.vs && a.battle == b.battle && a.result == b.result;
}

// 既定の矩形（1080p 換算。ENT-01 fixed_* の既定。2026-11-03 利用者が実機で決めた値。
// 調べた名前の位置は ENT-02 の表）
inline FixedRects default_fixed_rects()
{
	return FixedRects{{1045, 170, 1275, 210}, {1250, 1007, 1450, 1027}, {1140, 180, 1440, 238}};
}

} // namespace omask::core
