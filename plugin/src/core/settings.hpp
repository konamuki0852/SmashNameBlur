// フィルターの設定（データ設計書 ENT-01）。OBS から読んだ生の値を、制約に合わせて直す。
#pragma once

#include <cstdint>
#include <string>

#include "core/draw.hpp"
#include "core/params.hpp"
#include "core/types.hpp"

namespace omask::core {

constexpr int kSettingsVersion = 1;
// モザイクの強さ（ENT-01 mosaic_strength）の 1 マスの大きさ（1080p 換算）
constexpr int kMosaicWeakPx = 8;
constexpr int kMosaicNormalPx = 16;
constexpr int kMosaicStrongPx = 36;
constexpr uint32_t kFillColorDefault = 0xFF000000u; // 黒（0xAABBGGRR）

// 設定のキー（OBS の設定に保存する名前）
constexpr const char *kKeySettingsVersion = "settings_version";
constexpr const char *kKeyPolicy = "policy";
constexpr const char *kKeyDebugOverlay = "debug_overlay";
constexpr const char *kKeyStyle = "style";
constexpr const char *kKeyMosaicStrength = "mosaic_strength";
constexpr const char *kKeyFillColor = "fill_color";
constexpr const char *kKeyTagWidthPx = "tag_width_px";
constexpr const char *kKeyTagHeightPx = "tag_height_px";
constexpr const char *kKeyTagHoldMs = "tag_hold_ms";
constexpr const char *kKeyTagLookAheadMs = "tag_look_ahead_ms";
// 位置の決まった範囲（fixed_<vs|hud|result>_<x|y|w|h>_px。1080p 換算）
constexpr const char *kFixedRegionNames[3] = {"vs", "hud", "result"};
constexpr const char *kFixedFieldNames[4] = {"x", "y", "w", "h"};
std::string fixed_key(int region, int field); // 例 fixed_vs_x_px

// 値の名前（ENT-01）
constexpr const char *kPolicyLeakFirst = "leak_first";
constexpr const char *kPolicyFewerMosaic = "fewer_mosaic";
constexpr const char *kStyleMosaic = "mosaic";
constexpr const char *kStyleFill = "fill";
constexpr const char *kMosaicWeak = "weak";
constexpr const char *kMosaicNormal = "normal";
constexpr const char *kMosaicStrong = "strong";

// OBS の設定から読んだままの値
struct RawSettings {
	int64_t settings_version = kSettingsVersion;
	std::string policy = kPolicyLeakFirst;
	bool debug_overlay = false;
	std::string style = kStyleMosaic;
	std::string mosaic_strength = kMosaicNormal;
	int64_t fill_color = kFillColorDefault;
	int64_t tag_width_px = kTagWidthDefaultPx;
	int64_t tag_height_px = kTagHeightDefaultPx;
	int64_t tag_hold_ms = kTagHoldDefaultMs;
	int64_t tag_look_ahead_ms = kTagLookAheadDefaultMs;
	int64_t fixed[3][4] = {}; // [vs・hud・result][x・y・w・h]。has_fixed が偽なら使わない（既定の矩形）
	bool has_fixed = false;
};

// 直した設定（不変の写しとして使う）
struct Settings {
	Policy policy = Policy::kLeakFirst;
	DrawStyle draw;
	TagAdjust tag;
	FixedRects fixed = default_fixed_rects();
};

// 知らない版は既定値で読み直す。知らない決まり・種類は既定値、粗さと頭上タグの調整は範囲内に丸め、色は不透明にする
Settings normalize_settings(const RawSettings &raw);

// 既定の矩形を x・y・幅・高さにする（設定の既定値。[vs・hud・result][x・y・w・h]）
void fixed_rect_fields(const FixedRects &rects, int64_t out[3][4]);

} // namespace omask::core
