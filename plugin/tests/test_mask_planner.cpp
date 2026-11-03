// 出力するコマの隠す範囲（PRC-03 ＋ PRC-05 をまとめたもの）
#include "doctest/doctest.h"

#include "core/mask_planner.hpp"
#include "test_helpers.hpp"

using omask::core::MaskPlan;
using omask::core::MaskPlanner;
using omask::core::Policy;
using omask::core::RegionKind;
using omask::core::ResultStore;
using omask::core::ScreenKind;
using omask::core::TagCandidate;
using omask::core::tag_params_for;
using omask::test::make_result;
using omask::test::ms;
using omask::test::put_done;

TEST_CASE("バトルでは HUD の範囲と頭上タグの範囲を、位置の決まった範囲・頭上タグの順に返す")
{
	ResultStore store;
	put_done(store, make_result(ms(1000), ScreenKind::kBattle, {TagCandidate{320, 180, 0.9f}}));
	MaskPlanner planner;
	const MaskPlan plan = planner.plan(store, ms(1000), omask::core::default_fixed_rects(),
					   tag_params_for(Policy::kLeakFirst), true, true, 1920, 1080);
	REQUIRE(plan.regions.size() == 2);
	CHECK(plan.regions[0].kind == RegionKind::kFixed);
	CHECK(plan.regions[1].kind == RegionKind::kTag);
	CHECK(plan.fixed.battle);
	CHECK(plan.tag_count == 1);
	CHECK_FALSE(plan.late);
}

TEST_CASE("頭上タグの検出器が使えないときは、位置の決まった範囲だけ")
{
	ResultStore store;
	put_done(store, make_result(ms(1000), ScreenKind::kBattle, {TagCandidate{320, 180, 0.9f}}));
	MaskPlanner planner;
	const MaskPlan plan = planner.plan(store, ms(1000), omask::core::default_fixed_rects(),
					   tag_params_for(Policy::kLeakFirst), true, false, 1920, 1080);
	REQUIRE(plan.regions.size() == 1);
	CHECK(plan.regions[0].kind == RegionKind::kFixed);
}
