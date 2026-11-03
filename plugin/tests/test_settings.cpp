// 設定の読み込み（ENT-01）。テスト設計書 TC-12
#include "doctest/doctest.h"

#include "core/settings.hpp"

using omask::core::normalize_settings;
using omask::core::Policy;
using omask::core::RawSettings;
using omask::core::StyleKind;

TEST_CASE("TC-12 既定値")
{
	const auto s = normalize_settings(RawSettings{});
	CHECK(s.policy == Policy::kLeakFirst);
	CHECK(s.draw.kind == StyleKind::kMosaic);
	CHECK(s.draw.mosaic_block_px == 16); // 普通
	CHECK(s.draw.fill_color == 0xFF000000u);
	CHECK_FALSE(s.draw.debug_overlay);
}

TEST_CASE("TC-12 知らない決まり・種類は既定値、知っている値はそのまま")
{
	RawSettings raw;
	raw.policy = "unknown";
	raw.style = "blur";
	auto s = normalize_settings(raw);
	CHECK(s.policy == Policy::kLeakFirst);
	CHECK(s.draw.kind == StyleKind::kMosaic);

	raw.policy = "fewer_mosaic";
	raw.style = "fill";
	raw.debug_overlay = true;
	s = normalize_settings(raw);
	CHECK(s.policy == Policy::kFewerMosaic);
	CHECK(s.draw.kind == StyleKind::kFill);
	CHECK(s.draw.debug_overlay);
}

TEST_CASE("TC-12 モザイクの強さは弱い・普通・強いを 8・16・36 px にし、知らない値は普通")
{
	RawSettings raw;
	const char *inputs[] = {"weak", "normal", "strong", "unknown", ""};
	const int expected[] = {8, 16, 36, 16, 16};
	for (int i = 0; i < 5; ++i) {
		raw.mosaic_strength = inputs[i];
		CHECK(normalize_settings(raw).draw.mosaic_block_px == expected[i]);
	}
}

TEST_CASE("TC-12 半透明の色は不透明にし、知らない版は既定値で読み直す")
{
	RawSettings raw;
	raw.fill_color = 0x800000FF; // 半透明の赤
	CHECK(normalize_settings(raw).draw.fill_color == 0xFF0000FFu);

	raw.settings_version = 2;
	raw.policy = "fewer_mosaic";
	CHECK(normalize_settings(raw).policy == Policy::kLeakFirst);
}
