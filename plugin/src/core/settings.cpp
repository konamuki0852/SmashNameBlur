#include "core/settings.hpp"

#include <algorithm>

namespace omask::core {

namespace {

ReferenceRect rect_from_fields(const int64_t field[4])
{
	const auto x = static_cast<int>(std::clamp<int64_t>(field[0], 0, kReferenceWidth));
	const auto y = static_cast<int>(std::clamp<int64_t>(field[1], 0, kReferenceHeight));
	const auto w = static_cast<int>(std::clamp<int64_t>(field[2], 0, kReferenceWidth));
	const auto h = static_cast<int>(std::clamp<int64_t>(field[3], 0, kReferenceHeight));
	return ReferenceRect{x, y, std::min(x + w, kReferenceWidth), std::min(y + h, kReferenceHeight)};
}

} // namespace

std::string fixed_key(int region, int field)
{
	return std::string("fixed_") + kFixedRegionNames[region] + "_" + kFixedFieldNames[field] + "_px";
}

void fixed_rect_fields(const FixedRects &rects, int64_t out[3][4])
{
	const ReferenceRect *list[3] = {&rects.vs, &rects.battle, &rects.result};
	for (int i = 0; i < 3; ++i) {
		out[i][0] = list[i]->x0;
		out[i][1] = list[i]->y0;
		out[i][2] = list[i]->x1 - list[i]->x0;
		out[i][3] = list[i]->y1 - list[i]->y0;
	}
}

Settings normalize_settings(const RawSettings &raw)
{
	Settings settings;
	if (raw.settings_version != kSettingsVersion)
		return settings; // 知らない版: 既定値

	settings.policy = raw.policy == kPolicyFewerMosaic ? Policy::kFewerMosaic : Policy::kLeakFirst;
	settings.draw.kind = raw.style == kStyleFill ? StyleKind::kFill : StyleKind::kMosaic;
	settings.draw.debug_overlay = raw.debug_overlay;
	settings.draw.mosaic_block_px = raw.mosaic_strength == kMosaicWeak     ? kMosaicWeakPx
					: raw.mosaic_strength == kMosaicStrong ? kMosaicStrongPx
									       : kMosaicNormalPx; // 知らない値は普通
	settings.draw.fill_color = static_cast<uint32_t>(raw.fill_color) | 0xFF000000u; // 半透明は不透明にする
	settings.tag.width_px = static_cast<int>(std::clamp<int64_t>(raw.tag_width_px, kTagWidthMinPx, kTagWidthMaxPx));
	settings.tag.height_px =
		static_cast<int>(std::clamp<int64_t>(raw.tag_height_px, kTagHeightMinPx, kTagHeightMaxPx));
	settings.tag.hold_ms = static_cast<int>(std::clamp<int64_t>(raw.tag_hold_ms, kTagHoldMinMs, kTagHoldMaxMs));
	if (raw.has_fixed) {
		settings.fixed.vs = rect_from_fields(raw.fixed[0]);
		settings.fixed.battle = rect_from_fields(raw.fixed[1]);
		settings.fixed.result = rect_from_fields(raw.fixed[2]);
	}
	settings.tag.look_ahead_ms =
		static_cast<int>(std::clamp<int64_t>(raw.tag_look_ahead_ms, kTagLookAheadMinMs, kTagLookAheadMaxMs));
	return settings;
}

} // namespace omask::core
