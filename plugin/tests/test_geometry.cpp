// 座標の換算と切り詰め（PRC-03 手順 2・PRC-05 手順 2）。テスト設計書 TC-06
#include "doctest/doctest.h"

#include "core/fixed_policy.hpp"
#include "core/tag_policy.hpp"

using omask::core::FixedActive;
using omask::core::MaskRegion;
using omask::core::Policy;
using omask::core::PointF;
using omask::core::RectI;
using omask::core::TagDecision;
using omask::core::fixed_regions;
using omask::core::tag_params_for;
using omask::core::tag_regions;

namespace {

// 有効な範囲を vs・battle・result の順に並べて返す
std::vector<RectI> fixed_rects(int width, int height)
{
	std::vector<RectI> rects;
	for (const MaskRegion &region :
	     fixed_regions(FixedActive::all(), omask::core::default_fixed_rects(), width, height))
		rects.push_back(region.rect);
	return rects;
}

std::vector<RectI> tag_rects(std::vector<PointF> centers, int width, int height)
{
	TagDecision decision;
	decision.centers = std::move(centers);
	std::vector<RectI> rects;
	for (const MaskRegion &region : tag_regions(decision, tag_params_for(Policy::kLeakFirst), width, height))
		rects.push_back(region.rect);
	return rects;
}

} // namespace

TEST_CASE("TC-06 位置の決まった範囲（1920x1080。既定の矩形のまま）")
{
	const auto rects = fixed_rects(1920, 1080);
	REQUIRE(rects.size() == 3);
	CHECK(rects[0] == RectI{1045, 170, 1275, 210});
	CHECK(rects[1] == RectI{1250, 1007, 1450, 1027});
	CHECK(rects[2] == RectI{1140, 180, 1440, 238});
}

TEST_CASE("TC-06 位置の決まった範囲は、コマの大きさに比例して外側へ丸める（1280x720）")
{
	const auto rects = fixed_rects(1280, 720);
	REQUIRE(rects.size() == 3);
	CHECK(rects[0] == RectI{696, 113, 850, 140});
	CHECK(rects[1] == RectI{833, 671, 967, 685});
	CHECK(rects[2] == RectI{760, 120, 960, 159});
}

TEST_CASE("TC-06 頭上タグの範囲（解析座標の半幅 55・半高 16 をコマの大きさに比例させる）")
{
	CHECK(tag_rects({PointF{320, 180}}, 1920, 1080) == std::vector<RectI>{RectI{795, 492, 1125, 588}});
	CHECK(tag_rects({PointF{320, 180}}, 1280, 720) == std::vector<RectI>{RectI{530, 328, 750, 392}});
}

TEST_CASE("TC-06 コマの端にかかる範囲は切り詰め、外に出た範囲は出さない")
{
	CHECK(tag_rects({PointF{10, 5}}, 1920, 1080) == std::vector<RectI>{RectI{0, 0, 195, 63}});
	CHECK(tag_rects({PointF{635, 355}}, 1920, 1080) == std::vector<RectI>{RectI{1740, 1017, 1920, 1080}});
	CHECK(tag_rects({PointF{-100, -100}}, 1920, 1080).empty());
}

TEST_CASE("TC-06 同じ位置の候補は 1 つの範囲にまとめる")
{
	CHECK(tag_rects({PointF{320, 180}, PointF{320, 180}}, 1920, 1080).size() == 1);
}
