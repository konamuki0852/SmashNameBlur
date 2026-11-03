// 画面判定の扱い（PRC-02。白いコマの決まりを含む）。テスト設計書 TC-11
#include "doctest/doctest.h"

#include <array>
#include <cmath>

#include "core/screen_judge.hpp"
#include "test_helpers.hpp"

using omask::core::ScreenDecision;
using omask::core::ScreenJudge;
using omask::core::ScreenKind;
using omask::test::ms;

namespace {

// 最大の種類の確率が p（残りを他の 3 つで等分）になるロジット
std::array<float, 4> logits_for(int top, double p)
{
	std::array<float, 4> logits{};
	const double rest = (1.0 - p) / 3.0;
	for (int i = 0; i < 4; ++i)
		logits[static_cast<size_t>(i)] = static_cast<float>(std::log(i == top ? p : rest));
	return logits;
}

// 確率の組をそのままロジットにする
std::array<float, 4> logits_of(double other, double vs, double battle, double result)
{
	return {static_cast<float>(std::log(other)), static_cast<float>(std::log(vs)),
		static_cast<float>(std::log(battle)), static_cast<float>(std::log(result))};
}

} // namespace

TEST_CASE("TC-11 (a) 最大の確率が 0.8 以上ならその種類、未満なら不確か")
{
	ScreenJudge judge;
	const auto sure = logits_for(2, 0.8);
	const ScreenDecision battle = judge.decide(sure.data(), 0.3f, ms(1000));
	CHECK(battle.kind == ScreenKind::kBattle);
	CHECK(battle.probs[2] == doctest::Approx(0.8));

	const auto unsure = logits_for(1, 0.79);
	CHECK(judge.decide(unsure.data(), 0.3f, ms(1017)).kind == ScreenKind::kUncertain);

	const ScreenDecision none = judge.decide(nullptr, 0.3f, ms(1034));
	CHECK(none.kind == ScreenKind::kUncertain);
	CHECK_FALSE(none.has_probs);
}

TEST_CASE("TC-11 (b) 白いコマの決まり: 明るさ 0.85 超で、直前 3000 ms にバトル・リザルトが無いときだけ vs")
{
	const auto other = logits_for(0, 0.99);
	const auto battle = logits_for(2, 0.99);
	SUBCASE("直前にバトルが無い・0.86 → vs")
	{
		ScreenJudge judge;
		const ScreenDecision d = judge.decide(other.data(), 0.86f, ms(5000));
		CHECK(d.kind == ScreenKind::kVs);
		CHECK(d.by_brightness);
	}
	SUBCASE("0.85 ちょうどは対象外")
	{
		ScreenJudge judge;
		CHECK(judge.decide(other.data(), 0.85f, ms(5000)).kind == ScreenKind::kOther);
	}
	SUBCASE("3001 ms 前にバトル → vs")
	{
		ScreenJudge judge;
		judge.decide(battle.data(), 0.3f, ms(1999));
		CHECK(judge.decide(other.data(), 0.86f, ms(5000)).kind == ScreenKind::kVs);
	}
	SUBCASE("3000 ms 前ちょうどにバトル → screennet の結果のまま")
	{
		ScreenJudge judge;
		judge.decide(battle.data(), 0.3f, ms(2000));
		const ScreenDecision d = judge.decide(other.data(), 0.86f, ms(5000));
		CHECK(d.kind == ScreenKind::kOther);
		CHECK_FALSE(d.by_brightness);
	}
	SUBCASE("そのコマ自身がバトルなら止める（バトル中の白い閃光）")
	{
		ScreenJudge judge;
		CHECK(judge.decide(battle.data(), 0.95f, ms(5000)).kind == ScreenKind::kBattle);
	}
}

TEST_CASE("TC-11 (c) 不確かでバトルの確率 0.2 はバトルの判定として数え、0.19 は数えない")
{
	const auto other = logits_for(0, 0.99);
	SUBCASE("0.2")
	{
		ScreenJudge judge;
		const auto unsure = logits_of(0.6, 0.1, 0.2, 0.1);
		CHECK(judge.decide(unsure.data(), 0.3f, ms(4000)).kind == ScreenKind::kUncertain);
		CHECK(judge.decide(other.data(), 0.9f, ms(5000)).kind == ScreenKind::kOther);
	}
	SUBCASE("0.19")
	{
		ScreenJudge judge;
		const auto unsure = logits_of(0.61, 0.1, 0.19, 0.1);
		judge.decide(unsure.data(), 0.3f, ms(4000));
		CHECK(judge.decide(other.data(), 0.9f, ms(5000)).kind == ScreenKind::kVs);
	}
}
