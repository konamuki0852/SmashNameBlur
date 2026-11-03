// リザルトの相手のカードの名前の帯（PRC-14）と、位置の決まった範囲の矩形の設定（ENT-01 fixed_*）。
// テスト設計書 TC-50・51・54
#include "doctest/doctest.h"

#include <vector>

#include "core/mask_planner.hpp"
#include "core/preprocess.hpp"
#include "core/result_band.hpp"
#include "core/settings.hpp"
#include "test_helpers.hpp"

using omask::core::default_fixed_rects;
using omask::core::find_result_bands;
using omask::core::is_result_detail;
using omask::core::fixed_rect_fields;
using omask::core::kAnalysisWidth;
using omask::core::kTensorPlaneSize;
using omask::core::kTensorSize;
using omask::core::MaskPlan;
using omask::core::MaskPlanner;
using omask::core::MaskRegion;
using omask::core::normalize_settings;
using omask::core::Policy;
using omask::core::RawSettings;
using omask::core::RectI;
using omask::core::ReferenceRect;
using omask::core::ResultBand;
using omask::core::ResultBandParams;
using omask::core::ResultStore;
using omask::core::result_band_regions;
using omask::core::ScreenKind;
using omask::core::tag_params_for;
using omask::test::make_result;
using omask::test::ms;
using omask::test::put_done;

namespace {

// 灰色のテンソルに、行 [y0, y1)・列 [x0, x1) の黒を塗る
void paint_dark(std::vector<float> &tensor, int y0, int y1, int x0, int x1)
{
	for (int c = 0; c < 3; ++c)
		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x)
				tensor[c * kTensorPlaneSize + static_cast<size_t>(y) * kAnalysisWidth + x] = 0.05f;
}

// 相手のカードの列（1080p で x 1040〜1500 → 解析座標 347〜500）
constexpr int kColumnX0 = 347;
constexpr int kColumnX1 = 500;

omask::core::AnalysisResult with_band(int64_t t_ms, ScreenKind kind, int band_row, int rows = 12)
{
	omask::core::AnalysisResult result = make_result(ms(t_ms), kind);
	result.result_bands = {ResultBand{band_row, rows}};
	return result;
}

} // namespace

TEST_CASE("TC-50 右のカードの列で黒い帯の上端を見つける")
{
	std::vector<float> tensor(kTensorSize, 0.5f);
	SUBCASE("列の幅いっぱいの帯")
	{
		paint_dark(tensor, 287, 298, kColumnX0, kColumnX1);
		CHECK(find_result_bands(tensor.data()) == std::vector<ResultBand>{{287, 11}});
	}
	SUBCASE("黒が列の半分に満たない行・2 行だけの黒は帯としない")
	{
		paint_dark(tensor, 100, 110, kColumnX0, kColumnX0 + 60);
		paint_dark(tensor, 200, 202, kColumnX0, kColumnX1);
		CHECK(find_result_bands(tensor.data()).empty());
	}
	SUBCASE("画面の下の縁の黒は除く")
	{
		paint_dark(tensor, 350, 360, kColumnX0, kColumnX1);
		CHECK(find_result_bands(tensor.data()).empty());
	}
	SUBCASE("上下に 2 つ")
	{
		paint_dark(tensor, 50, 55, kColumnX0, kColumnX1);
		paint_dark(tensor, 111, 121, kColumnX0, kColumnX1);
		CHECK(find_result_bands(tensor.data()) == std::vector<ResultBand>{{50, 5}, {111, 10}});
	}
}

TEST_CASE("TC-50 帯の上端から上 40 px・下 220 px を、カードの列の幅で隠す（保持 200 ms・先読み 100 ms）")
{
	ResultStore store;
	for (int64_t t = 700; t <= 1500; t += 10)
		put_done(store, make_result(ms(t), ScreenKind::kResult));
	put_done(store, with_band(1000, ScreenKind::kResult, 287)); // 1080p で 861
	const ResultBandParams params;

	const auto at = [&](int64_t t) {
		return result_band_regions(store, ms(t), params, 1920, 1080);
	};
	const std::vector<MaskRegion> regions = at(1000);
	REQUIRE(regions.size() == 1);
	CHECK(regions[0].rect == RectI{1032, 813, 1508, 1080});
	CHECK(at(900).size() == 1);
	CHECK(at(890).empty());
	CHECK(at(1200).size() == 1);
	CHECK(at(1210).empty());

	const std::vector<MaskRegion> small = result_band_regions(store, ms(1000), params, 1280, 720);
	REQUIRE(small.size() == 1);
	CHECK(small[0].rect == RectI{688, 542, 1006, 720});
}

TEST_CASE("TC-50 帯の範囲を足すのはリザルトの範囲が有効なときだけ")
{
	ResultStore store;
	MaskPlanner planner;
	SUBCASE("リザルト: 決まった範囲と帯の範囲")
	{
		put_done(store, with_band(1000, ScreenKind::kResult, 287));
		const MaskPlan plan = planner.plan(store, ms(1000), default_fixed_rects(),
						   tag_params_for(Policy::kLeakFirst), true, true, 1920, 1080);
		CHECK(plan.fixed.result);
		REQUIRE(plan.regions.size() == 2);
		CHECK(plan.regions[0].rect == RectI{1140, 180, 1440, 238});
		CHECK(plan.regions[1].rect == RectI{1032, 813, 1508, 1080});
	}
	SUBCASE("バトル: 帯があっても足さない")
	{
		put_done(store, with_band(1000, ScreenKind::kBattle, 287));
		const MaskPlan plan = planner.plan(store, ms(1000), default_fixed_rects(),
						   tag_params_for(Policy::kLeakFirst), true, true, 1920, 1080);
		CHECK_FALSE(plan.fixed.result);
		REQUIRE(plan.regions.size() == 1);
	}
}

TEST_CASE("TC-51 位置の決まった範囲の矩形は設定の x・y・幅・高さで置き換える")
{
	SUBCASE("既定は利用者が決めた矩形")
	{
		const auto s = normalize_settings(RawSettings{});
		CHECK(s.fixed == default_fixed_rects());
		int64_t fields[3][4] = {};
		fixed_rect_fields(s.fixed, fields);
		CHECK(fields[0][0] == 1045);
		CHECK(fields[0][1] == 170);
		CHECK(fields[0][2] == 230);
		CHECK(fields[0][3] == 40);
		CHECK(fields[1][0] == 1250);
		CHECK(fields[1][1] == 1007);
		CHECK(fields[1][2] == 200);
		CHECK(fields[1][3] == 20);
		CHECK(fields[2][0] == 1140);
		CHECK(fields[2][1] == 180);
		CHECK(fields[2][2] == 300);
		CHECK(fields[2][3] == 58);
	}
	SUBCASE("値を使い、画面の外は切り詰める")
	{
		RawSettings raw;
		raw.has_fixed = true;
		fixed_rect_fields(default_fixed_rects(), raw.fixed);
		raw.fixed[0][0] = 900;
		raw.fixed[0][2] = 500;
		raw.fixed[1][1] = 1000;
		raw.fixed[1][3] = 500;
		raw.fixed[2][0] = -20;
		raw.fixed[2][2] = 0;
		const auto s = normalize_settings(raw);
		CHECK(s.fixed.vs == ReferenceRect{900, 170, 1400, 210});
		CHECK(s.fixed.battle.y0 == 1000);
		CHECK(s.fixed.battle.y1 == 1080);
		CHECK(s.fixed.result.x0 == 0);
		CHECK(s.fixed.result.x1 == 0); // 幅 0: 隠さない
	}
}

TEST_CASE("TC-54 結果詳細（上端 100〜200・太さ 7 行以上の帯）のコマの帯は使わない")
{
	SUBCASE("見分け")
	{
		CHECK(is_result_detail({{49, 8}}));          // 1080p で 147。結果詳細
		CHECK(is_result_detail({{28, 6}, {49, 8}})); // 上の細い帯と一緒でも
		CHECK_FALSE(is_result_detail({{53, 3}}));    // 入ってくる途中・スライド中の細い帯
		CHECK_FALSE(is_result_detail({{5, 12}}));    // 入ってくる途中（上端 15）
		CHECK_FALSE(is_result_detail({{287, 12}}));  // 対戦カード表示（上端 861）
		CHECK_FALSE(is_result_detail({{72, 8}}));    // 上端 216
	}
	SUBCASE("結果詳細のコマの帯から範囲を作らず、前後のコマの帯は使う")
	{
		ResultStore store;
		put_done(store, with_band(1000, ScreenKind::kResult, 49, 8));
		const ResultBandParams params;
		CHECK(result_band_regions(store, ms(1000), params, 1920, 1080).empty());
		put_done(store, with_band(950, ScreenKind::kResult, 72, 8)); // スライドの終わりの帯（保持で残る）
		const std::vector<MaskRegion> regions = result_band_regions(store, ms(1000), params, 1920, 1080);
		REQUIRE(regions.size() == 1);
		CHECK(regions[0].rect == RectI{1032, 168, 1508, 444});
	}
}
