// 位置の決まった範囲（PRC-03）。テスト設計書 TC-05
#include "doctest/doctest.h"

#include "core/fixed_policy.hpp"
#include "test_helpers.hpp"

using omask::core::AnalysisResult;
using omask::core::FixedActive;
using omask::core::FixedParams;
using omask::core::ResultStore;
using omask::core::ScreenKind;
using omask::core::decide_fixed;
using omask::test::fill_done;
using omask::test::make_result;
using omask::test::ms;
using omask::test::put_done;

namespace {

FixedActive decide_at(const ResultStore &store, int64_t output_ms, bool screen_model_available = true)
{
	return decide_fixed(store, ms(output_ms), FixedParams{}, screen_model_available);
}

AnalysisResult uncertain(int64_t t_ms, float vs, float battle, float result)
{
	AnalysisResult r = make_result(ms(t_ms), ScreenKind::kUncertain);
	r.screen_probs = {1.0f - vs - battle - result, vs, battle, result};
	return r;
}

} // namespace

TEST_CASE("TC-05 その画面の種類の範囲だけ有効になる")
{
	SUBCASE("vs")
	{
		ResultStore store;
		put_done(store, make_result(ms(1000), ScreenKind::kVs));
		CHECK(decide_at(store, 1000) == FixedActive{true, false, false});
	}
	SUBCASE("battle")
	{
		ResultStore store;
		put_done(store, make_result(ms(1000), ScreenKind::kBattle));
		CHECK(decide_at(store, 1000) == FixedActive{false, true, false});
	}
	SUBCASE("result")
	{
		ResultStore store;
		put_done(store, make_result(ms(1000), ScreenKind::kResult));
		CHECK(decide_at(store, 1000) == FixedActive{false, false, true});
	}
	SUBCASE("other だけなら無効")
	{
		ResultStore store;
		put_done(store, make_result(ms(1000), ScreenKind::kOther));
		CHECK(decide_at(store, 1000) == FixedActive{});
	}
}

TEST_CASE("TC-05 不確かなときは、その種類の確率が下限 0.2 以上なら有効")
{
	SUBCASE("0.1 は無効")
	{
		ResultStore store;
		put_done(store, uncertain(1000, 0.1f, 0.0f, 0.0f));
		CHECK_FALSE(decide_at(store, 1000).vs);
	}
	SUBCASE("0.2 は有効")
	{
		ResultStore store;
		put_done(store, uncertain(1000, 0.2f, 0.0f, 0.0f));
		CHECK(decide_at(store, 1000).vs);
	}
	SUBCASE("0.3 は有効")
	{
		ResultStore store;
		put_done(store, uncertain(1000, 0.0f, 0.3f, 0.0f));
		CHECK(decide_at(store, 1000) == FixedActive{false, true, false});
	}
}

TEST_CASE("TC-05 安全側: 解析なし・失敗・確率なし・結果なし・screennet が使えないときは全部有効")
{
	SUBCASE("解析なし")
	{
		ResultStore store;
		fill_done(store, 900, 1100, 50, ScreenKind::kOther);
		store.mark_not_analyzed(ms(1020));
		CHECK(decide_at(store, 1000) == FixedActive::all());
	}
	SUBCASE("推論の失敗")
	{
		ResultStore store;
		fill_done(store, 900, 1100, 50, ScreenKind::kOther);
		AnalysisResult failed = make_result(ms(1020), ScreenKind::kOther);
		failed.status = omask::core::AnalysisStatus::kFailed;
		put_done(store, failed);
		CHECK(decide_at(store, 1000) == FixedActive::all());
	}
	SUBCASE("確率の無い不確か")
	{
		ResultStore store;
		AnalysisResult r = make_result(ms(1000), ScreenKind::kUncertain);
		r.has_screen_probs = false;
		put_done(store, r);
		CHECK(decide_at(store, 1000) == FixedActive::all());
	}
	SUBCASE("結果が 1 つも無い（受け取り済みで解析待ちだけ）")
	{
		ResultStore store;
		store.add_frame(ms(1000));
		CHECK(decide_at(store, 1000) == FixedActive::all());
	}
	SUBCASE("screennet が使えない")
	{
		ResultStore store;
		put_done(store, make_result(ms(1000), ScreenKind::kOther));
		CHECK(decide_at(store, 1000, false) == FixedActive::all());
	}
}

TEST_CASE("TC-05 切り替わりの前 100 ms・後 500 ms は有効（端の ±1 ms で切り替わる）")
{
	ResultStore store;
	for (int64_t t = 0; t <= 3000; t += 50) {
		const ScreenKind kind = t == 1000 ? ScreenKind::kVs : ScreenKind::kOther;
		put_done(store, make_result(ms(t), kind));
	}
	CHECK_FALSE(decide_at(store, 899).vs);
	CHECK(decide_at(store, 900).vs);
	CHECK(decide_at(store, 1500).vs);
	CHECK_FALSE(decide_at(store, 1501).vs);
}
