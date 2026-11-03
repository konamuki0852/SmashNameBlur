// 頭上タグの隠し方の調整（ENT-01 tag_*・PRC-05 パラメータの組み立て）。テスト設計書 TC-45・46
#include "doctest/doctest.h"

#include <vector>

#include "core/mask_planner.hpp"
#include "core/params.hpp"
#include "core/settings.hpp"
#include "core/tag_policy.hpp"
#include "test_helpers.hpp"

using omask::core::apply_tag_adjust;
using omask::core::kNsPerMs;
using omask::core::MaskPlan;
using omask::core::MaskPlanner;
using omask::core::normalize_settings;
using omask::core::Policy;
using omask::core::RawSettings;
using omask::core::ResultStore;
using omask::core::ScreenKind;
using omask::core::TagAdjust;
using omask::core::TagCandidate;
using omask::core::TagDecision;
using omask::core::TagMemory;
using omask::core::TagParams;
using omask::core::decide_tags;
using omask::core::delay_look_ahead_ms;
using omask::core::result_keep_ms;
using omask::core::tag_params_for;
using omask::test::contains;
using omask::test::make_result;
using omask::test::ms;
using omask::test::put_done;

namespace {

TagAdjust read_adjust(int64_t width, int64_t height, int64_t hold, int64_t look_ahead = 100)
{
	RawSettings raw;
	raw.tag_width_px = width;
	raw.tag_height_px = height;
	raw.tag_hold_ms = hold;
	raw.tag_look_ahead_ms = look_ahead;
	return normalize_settings(raw).tag;
}

// 1000 ms に強い候補が 1 回だけ見つかり、前後は候補の無い解析済みの結果
ResultStore single_tag_store()
{
	ResultStore store;
	omask::test::fill_done(store, 0, 990, 10);
	put_done(store, make_result(ms(1000), ScreenKind::kBattle, {TagCandidate{320, 180, 0.9f}}));
	omask::test::fill_done(store, 1010, 2300, 10);
	return store;
}

bool tag_at(const ResultStore &store, int64_t output_ms, const TagParams &params)
{
	TagMemory memory;
	const TagDecision decision = decide_tags(store, ms(output_ms), params, memory);
	return contains(decision.centers, 320, 180);
}

} // namespace

TEST_CASE("TC-45 既定は利用者が決めた値（160x48 px・先読み 32 ms・保持 32 ms）で、閾値・つなぐ時間は組のまま")
{
	const TagAdjust adjust = normalize_settings(RawSettings{}).tag;
	CHECK(adjust.width_px == 160);
	CHECK(adjust.height_px == 48);
	CHECK(adjust.hold_ms == 32);
	CHECK(adjust.look_ahead_ms == 32);

	const TagParams base = tag_params_for(Policy::kLeakFirst);
	const TagParams applied = apply_tag_adjust(base, adjust);
	CHECK(applied.half_width == doctest::Approx(160.0f * 640 / 1920 / 2)); // 解析座標の半幅
	CHECK(applied.half_height == doctest::Approx(8.0f));
	CHECK(applied.hold_ms == 32);
	CHECK(applied.look_ahead_ms == 32);
	CHECK(applied.find_threshold == base.find_threshold);
	CHECK(applied.link_threshold == base.link_threshold);
	CHECK(applied.link_before_ms == base.link_before_ms);
	CHECK(applied.link_after_ms == base.link_after_ms);
}

TEST_CASE("TC-45 範囲の外は丸める")
{
	const std::vector<int64_t> widths = {-10, 0, 330, 600, 900};
	const std::vector<int> expected_widths = {0, 0, 330, 600, 600};
	const std::vector<int64_t> heights = {-5, 0, 96, 200, 500};
	const std::vector<int> expected_heights = {0, 0, 96, 200, 200};
	const std::vector<int64_t> holds = {-1, 0, 500, 1000, 5000};
	const std::vector<int> expected_holds = {0, 0, 500, 1000, 1000};
	const std::vector<int64_t> looks = {-1, 0, 50, 100, 500};
	const std::vector<int> expected_looks = {0, 0, 50, 100, 100};
	for (size_t i = 0; i < widths.size(); ++i) {
		CAPTURE(i);
		const TagAdjust adjust = read_adjust(widths[i], heights[i], holds[i], looks[i]);
		CHECK(adjust.look_ahead_ms == expected_looks[i]);
		CHECK(adjust.width_px == expected_widths[i]);
		CHECK(adjust.height_px == expected_heights[i]);
		CHECK(adjust.hold_ms == expected_holds[i]);
	}
}

TEST_CASE("TC-45 置き換えるのは範囲の大きさ・保持・先読みだけ")
{
	const TagParams base = tag_params_for(Policy::kLeakFirst);
	const TagParams applied = apply_tag_adjust(base, read_adjust(180, 40, 0));
	CHECK(applied.half_width == doctest::Approx(30.0f)); // 180 px ÷ 3 ÷ 2
	CHECK(applied.half_height == doctest::Approx(20.0f / 3.0f));
	CHECK(applied.hold_ms == 0);
	CHECK(applied.find_threshold == base.find_threshold);
	CHECK(applied.link_threshold == base.link_threshold);
	CHECK(applied.link_before_ms == base.link_before_ms);
	CHECK(applied.link_after_ms == base.link_after_ms);
}

TEST_CASE("TC-46 保持の長さまで範囲が残る")
{
	const ResultStore store = single_tag_store();
	const TagParams base = tag_params_for(Policy::kLeakFirst);

	SUBCASE("0 ms: 見つかったコマと先読みの分だけ")
	{
		const TagParams params = apply_tag_adjust(base, read_adjust(330, 96, 0));
		CHECK(tag_at(store, 900, params)); // 先読み 100 ms
		CHECK(tag_at(store, 1000, params));
		CHECK_FALSE(tag_at(store, 1010, params));
	}
	SUBCASE("200 ms")
	{
		const TagParams params = apply_tag_adjust(base, read_adjust(330, 96, 200));
		CHECK(tag_at(store, 1200, params));
		CHECK_FALSE(tag_at(store, 1210, params));
	}
	SUBCASE("1000 ms")
	{
		const TagParams params = apply_tag_adjust(base, read_adjust(330, 96, 1000));
		CHECK(tag_at(store, 2000, params));
		CHECK_FALSE(tag_at(store, 2010, params));
	}
}

TEST_CASE("TC-46 保持の最大でも、結果の置き場に残す長さが足りる")
{
	ResultStore store = single_tag_store();
	const TagParams params = apply_tag_adjust(tag_params_for(Policy::kLeakFirst), read_adjust(330, 96, 1000));
	MaskPlanner planner;
	for (int64_t t = 1000; t <= 2000; t += 10) {
		CAPTURE(t);
		const MaskPlan plan =
			planner.plan(store, ms(t), omask::core::default_fixed_rects(), params, true, true, 1920, 1080);
		CHECK(plan.tag_count == 1);
		store.prune_before(ms(t) - result_keep_ms() * kNsPerMs); // プラグインと同じ順で捨てる
	}
}

TEST_CASE("TC-46 範囲の幅と高さが設定に比例する")
{
	const ResultStore store = single_tag_store();
	const TagParams base = tag_params_for(Policy::kLeakFirst);
	struct Case {
		int width_px;
		int height_px;
		int frame_width;
		int frame_height;
		int expected_width;
	};
	const std::vector<Case> cases = {
		{180, 40, 1920, 1080, 180},
		{600, 200, 1920, 1080, 600},
		{600, 200, 1280, 720, 400}, // 720p では 2/3
	};
	for (const Case &c : cases) {
		CAPTURE(c.width_px);
		CAPTURE(c.frame_width);
		MaskPlanner planner;
		const TagParams params = apply_tag_adjust(base, read_adjust(c.width_px, c.height_px, 500));
		const MaskPlan plan = planner.plan(store, ms(1000), omask::core::default_fixed_rects(), params, true,
						   true, c.frame_width, c.frame_height);
		REQUIRE(plan.tag_count == 1);
		const auto &rect = plan.regions.back().rect;
		CHECK(rect.width() == c.expected_width);
		const int expected_height = c.height_px * c.frame_height / 1080;
		CHECK(rect.height() >= expected_height);
		CHECK(rect.height() <= expected_height + 2); // 切り上げ・切り捨ての分
	}
}

TEST_CASE("TC-46 幅か高さが 0 なら頭上タグは隠さない")
{
	const ResultStore store = single_tag_store();
	const TagParams base = tag_params_for(Policy::kLeakFirst);
	struct Case {
		int width_px;
		int height_px;
		int expected_count;
	};
	const std::vector<Case> cases = {{0, 96, 0}, {330, 0, 0}, {0, 0, 0}, {10, 4, 1}};
	for (const Case &c : cases) {
		CAPTURE(c.width_px);
		CAPTURE(c.height_px);
		MaskPlanner planner;
		const TagParams params = apply_tag_adjust(base, read_adjust(c.width_px, c.height_px, 500));
		const MaskPlan plan = planner.plan(store, ms(1000), omask::core::default_fixed_rects(), params, true,
						   true, 1920, 1080);
		CHECK(plan.tag_count == c.expected_count);
	}
}

TEST_CASE("TC-46 先読みの長さだけ前のコマから範囲が出る")
{
	const ResultStore store = single_tag_store();
	const TagParams base = tag_params_for(Policy::kLeakFirst);
	SUBCASE("0 ms: 見つかったコマから")
	{
		const TagParams params = apply_tag_adjust(base, read_adjust(330, 96, 500, 0));
		CHECK_FALSE(tag_at(store, 990, params));
		CHECK(tag_at(store, 1000, params));
	}
	SUBCASE("50 ms")
	{
		const TagParams params = apply_tag_adjust(base, read_adjust(330, 96, 500, 50));
		CHECK_FALSE(tag_at(store, 940, params));
		CHECK(tag_at(store, 950, params));
	}
	SUBCASE("100 ms（既定）")
	{
		const TagParams params = apply_tag_adjust(base, read_adjust(330, 96, 500, 100));
		CHECK_FALSE(tag_at(store, 890, params));
		CHECK(tag_at(store, 900, params));
	}
	// 先読みの上限は遅延 D の先読み以下（利用者の値で映像の遅れが変わらない）
	CHECK(omask::core::kTagLookAheadMaxMs <= delay_look_ahead_ms());
	CHECK(delay_look_ahead_ms() == 100);
}
