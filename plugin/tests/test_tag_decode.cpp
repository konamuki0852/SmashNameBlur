// 頭上タグの候補の取り出し（PRC-04 手順 4・5）。テスト設計書 TC-10
#include "doctest/doctest.h"

#include <cmath>
#include <vector>

#include "core/tag_decode.hpp"

using omask::core::decode_tag_candidates;
using omask::core::kTagGridHeight;
using omask::core::kTagGridWidth;
using omask::core::TagCandidate;

namespace {

class Output {
public:
	Output() : data_(3 * kPlane, 0.0f)
	{
		for (size_t i = 0; i < kPlane; ++i)
			data_[i] = -10.0f; // 中心らしさは低い
	}
	void set(int x, int y, float logit, float off_x = 0.5f, float off_y = 0.5f)
	{
		const size_t i = static_cast<size_t>(y) * kTagGridWidth + x;
		data_[i] = logit;
		data_[kPlane + i] = off_x;
		data_[2 * kPlane + i] = off_y;
	}
	const float *data() const { return data_.data(); }

private:
	static constexpr size_t kPlane = static_cast<size_t>(kTagGridWidth) * kTagGridHeight;
	std::vector<float> data_;
};

bool has(const std::vector<TagCandidate> &cs, float x, float y)
{
	for (const auto &c : cs) {
		if (std::abs(c.x - x) < 1e-4f && std::abs(c.y - y) < 1e-4f)
			return true;
	}
	return false;
}

} // namespace

TEST_CASE("TC-10 山が 2 つ: 位置は（格子＋ずれ）× 8、確からしさはシグモイド")
{
	Output out;
	out.set(10, 5, 2.0f, 0.25f, 0.75f);
	out.set(40, 20, 1.0f);
	const auto cs = decode_tag_candidates(out.data(), 0.3f);
	REQUIRE(cs.size() == 2);
	CHECK(has(cs, 82.0f, 46.0f));
	CHECK(has(cs, 324.0f, 164.0f));
	CHECK(cs[0].score == doctest::Approx(1.0 / (1.0 + std::exp(-2.0))));
}

TEST_CASE("TC-10 3x3 で最大でない格子は出さず、同じ値の隣り合う山は両方出す")
{
	Output out;
	out.set(40, 20, 1.0f);
	out.set(41, 20, 0.5f); // 隣より低い
	out.set(60, 30, 1.5f);
	out.set(61, 30, 1.5f); // 同じ値の隣
	const auto cs = decode_tag_candidates(out.data(), 0.3f);
	CHECK(cs.size() == 3);
	CHECK(has(cs, 324.0f, 164.0f));
	CHECK_FALSE(has(cs, 332.0f, 164.0f));
	CHECK(has(cs, 484.0f, 244.0f));
	CHECK(has(cs, 492.0f, 244.0f));
}

TEST_CASE("TC-10 閾値ちょうどは出し、わずかに下は出さない。端の格子も出せる")
{
	Output out;
	const float at_threshold = std::log(0.3f / (1.0f - 0.3f));
	out.set(70, 40, at_threshold);
	out.set(5, 40, std::nextafter(at_threshold, -100.0f));
	out.set(0, 0, 1.0f, 0.0f, 0.0f);
	const auto cs = decode_tag_candidates(out.data(), 0.3f);
	CHECK(has(cs, 564.0f, 324.0f));
	CHECK_FALSE(has(cs, 44.0f, 324.0f));
	CHECK(has(cs, 0.0f, 0.0f));
}
