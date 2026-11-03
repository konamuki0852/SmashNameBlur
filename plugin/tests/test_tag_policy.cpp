// 頭上タグの範囲の決まり（PRC-05）。テスト設計書 TC-01〜04
#include "doctest/doctest.h"

#include "core/tag_policy.hpp"
#include "test_helpers.hpp"

using omask::core::Policy;
using omask::core::ResultStore;
using omask::core::ScreenKind;
using omask::core::TagCandidate;
using omask::core::TagDecision;
using omask::core::TagMemory;
using omask::core::decide_tags;
using omask::core::tag_params_for;
using omask::test::contains;
using omask::test::make_result;
using omask::test::ms;
using omask::test::put_done;

namespace {

TagDecision decide_at(const ResultStore &store, int64_t output_ms, Policy policy = Policy::kLeakFirst)
{
	TagMemory memory;
	return decide_tags(store, ms(output_ms), tag_params_for(policy), memory);
}

} // namespace

TEST_CASE("TC-01 二段の閾値: 見つける閾値以上はそのまま採る")
{
	ResultStore store;
	put_done(store, make_result(ms(1000), ScreenKind::kBattle, {TagCandidate{100, 100, 0.6f}}));
	const TagDecision decision = decide_at(store, 1000);
	CHECK(contains(decision.centers, 100, 100));
}

TEST_CASE("TC-01 二段の閾値: つなぐ閾値の候補は、近い時間に近い位置の見つけた候補があれば採る")
{
	ResultStore store;
	put_done(store, make_result(ms(500), ScreenKind::kBattle, {TagCandidate{110, 105, 0.6f}}));
	put_done(store, make_result(ms(1000), ScreenKind::kBattle, {TagCandidate{100, 100, 0.4f}}));
	const TagDecision decision = decide_at(store, 1000);
	CHECK(contains(decision.centers, 100, 100));
}

TEST_CASE("TC-01 二段の閾値: つなぐ閾値の候補だけなら採らない")
{
	ResultStore store;
	put_done(store, make_result(ms(1000), ScreenKind::kBattle, {TagCandidate{100, 100, 0.4f}}));
	const TagDecision decision = decide_at(store, 1000);
	CHECK(decision.centers.empty());
}

TEST_CASE("TC-01 二段の閾値: つなぐ距離ちょうどは採り、1 px 超えると採らない")
{
	SUBCASE("横 40 px ちょうど")
	{
		ResultStore store;
		put_done(store, make_result(ms(1000), ScreenKind::kBattle,
					    {TagCandidate{100, 100, 0.4f}, TagCandidate{140, 100, 0.6f}}));
		CHECK(contains(decide_at(store, 1000).centers, 100, 100));
	}
	SUBCASE("横 41 px")
	{
		ResultStore store;
		put_done(store, make_result(ms(1000), ScreenKind::kBattle,
					    {TagCandidate{100, 100, 0.4f}, TagCandidate{141, 100, 0.6f}}));
		CHECK_FALSE(contains(decide_at(store, 1000).centers, 100, 100));
	}
	SUBCASE("縦 20 px ちょうど")
	{
		ResultStore store;
		put_done(store, make_result(ms(1000), ScreenKind::kBattle,
					    {TagCandidate{100, 100, 0.4f}, TagCandidate{100, 120, 0.6f}}));
		CHECK(contains(decide_at(store, 1000).centers, 100, 100));
	}
	SUBCASE("縦 21 px")
	{
		ResultStore store;
		put_done(store, make_result(ms(1000), ScreenKind::kBattle,
					    {TagCandidate{100, 100, 0.4f}, TagCandidate{100, 121, 0.6f}}));
		CHECK_FALSE(contains(decide_at(store, 1000).centers, 100, 100));
	}
	SUBCASE("時間差に応じて広がる（10 コマ離れると横 40 + 40 px まで）")
	{
		ResultStore store;
		const int64_t ten_frames_ns = 166666667; // 60fps の 10 コマ
		put_done(store, make_result(ms(1000), ScreenKind::kBattle, {TagCandidate{100, 100, 0.4f}}));
		put_done(store,
			 make_result(ms(1000) - ten_frames_ns, ScreenKind::kBattle, {TagCandidate{179, 100, 0.6f}}));
		CHECK(contains(decide_at(store, 1000).centers, 100, 100));
	}
}

TEST_CASE("TC-02 先読みと保持の時間幅（端の ±1 ms で切り替わる）")
{
	ResultStore store;
	for (int64_t t = 1000; t <= 1500; t += 100)
		put_done(store, make_result(ms(t), ScreenKind::kBattle, {TagCandidate{320, 180, 0.6f}}));

	SUBCASE("漏れ最優先: 900 ms から 2000 ms まで")
	{
		CHECK(decide_at(store, 899).centers.empty());
		CHECK_FALSE(decide_at(store, 900).centers.empty());
		CHECK_FALSE(decide_at(store, 2000).centers.empty());
		CHECK(decide_at(store, 2001).centers.empty());
	}
	SUBCASE("モザイク少なめ: 900 ms から 1700 ms まで")
	{
		CHECK(decide_at(store, 899, Policy::kFewerMosaic).centers.empty());
		CHECK_FALSE(decide_at(store, 900, Policy::kFewerMosaic).centers.empty());
		CHECK_FALSE(decide_at(store, 1700, Policy::kFewerMosaic).centers.empty());
		CHECK(decide_at(store, 1701, Policy::kFewerMosaic).centers.empty());
	}
}

TEST_CASE("TC-03 因果的なつなぎ（出力の時点で解析が済んでいない結果はつなぐ相手にしない）")
{
	ResultStore store;
	put_done(store, make_result(ms(1000), ScreenKind::kBattle, {TagCandidate{100, 100, 0.4f}}));

	SUBCASE("解析済み: 50 ms 後の見つけた候補とつながる")
	{
		put_done(store, make_result(ms(1050), ScreenKind::kBattle, {TagCandidate{110, 102, 0.6f}}));
		CHECK(contains(decide_at(store, 1000).centers, 100, 100));
	}
	SUBCASE("未解析: 50 ms 後のコマは受け取り済みだが結果が無い")
	{
		store.add_frame(ms(1050));
		const TagDecision decision = decide_at(store, 1000);
		CHECK_FALSE(contains(decision.centers, 100, 100));
		CHECK(decision.late);
	}
}

TEST_CASE("TC-04 間に合わないときの扱い（数えて、直前の範囲を保持＋先読みまで延長する）")
{
	ResultStore store;
	const auto params = tag_params_for(Policy::kLeakFirst);
	for (int64_t t = 1000; t <= 1100; t += 20)
		put_done(store, make_result(ms(t), ScreenKind::kBattle, {TagCandidate{320, 180, 0.6f}}));
	omask::test::fill_done(store, 1120, 1690, 10);
	for (int64_t t = 1700; t <= 1810; t += 10)
		store.add_frame(ms(t)); // 受け取り済みで解析が間に合っていない

	TagMemory memory;
	const TagDecision first = decide_tags(store, ms(1100), params, memory);
	CHECK_FALSE(first.late);
	CHECK(contains(first.centers, 320, 180));

	const TagDecision at_limit = decide_tags(store, ms(1700), params, memory);
	CHECK(at_limit.late);
	CHECK(contains(at_limit.centers, 320, 180)); // 1100 ms から 保持 500 ＋ 先読み 100 = 600 ms まで延長

	const TagDecision after_limit = decide_tags(store, ms(1701), params, memory);
	CHECK(after_limit.late);
	CHECK(after_limit.centers.empty());
}

TEST_CASE("TC-04 解析なし・失敗のコマも間に合わないとして数える")
{
	ResultStore store;
	omask::test::fill_done(store, 900, 1000, 10);
	store.mark_not_analyzed(ms(1050));
	auto failed = make_result(ms(1080));
	failed.status = omask::core::AnalysisStatus::kFailed;
	put_done(store, failed);
	CHECK(decide_at(store, 1000).late);
	CHECK_FALSE(decide_at(store, 900).late);
}
