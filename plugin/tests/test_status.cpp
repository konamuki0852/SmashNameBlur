// 状態の遷移と数え方（PRC-09、画面設計書 図 5-2）。テスト設計書 TC-15
#include "doctest/doctest.h"

#include "core/status.hpp"
#include "test_helpers.hpp"

using omask::core::EngineState;
using omask::core::LogLimiter;
using omask::core::StatusMonitor;
using omask::core::UnavailableReason;
using omask::test::ms;

TEST_CASE("TC-15 準備 → 動作中（GPU）→ GPU の失敗で CPU → 対応外の形式 → 戻る")
{
	StatusMonitor m;
	CHECK(m.state() == EngineState::kPreparing);
	m.on_engine_ready(true);
	CHECK(m.state() == EngineState::kRunningGpu);
	m.on_gpu_fallback();
	CHECK(m.state() == EngineState::kRunningCpu);
	m.on_format(false);
	CHECK(m.state() == EngineState::kUnsupportedFormat);
	m.on_format(true);
	CHECK(m.state() == EngineState::kRunningCpu);
}

TEST_CASE("TC-15 準備に失敗すると検出できない（理由を持つ）")
{
	StatusMonitor m;
	m.on_engine_failed(UnavailableReason::kModelMissing);
	CHECK(m.state() == EngineState::kUnavailable);
	CHECK(m.reason() == UnavailableReason::kModelMissing);
}

TEST_CASE("TC-15 直近 60 秒に 5 回間に合わないと追いついていない、0 回で戻る")
{
	StatusMonitor m;
	m.on_engine_ready(true);
	for (int i = 0; i < 4; ++i)
		m.on_late(ms(1000) + ms(i * 1000));
	CHECK(m.state() == EngineState::kRunningGpu);
	m.on_late(ms(5000));
	CHECK(m.state() == EngineState::kLagging);
	m.on_tick(ms(60000)); // まだ 60 秒以内に 5 回ある（1〜5 秒の 5 回）
	CHECK(m.state() == EngineState::kLagging);
	m.on_tick(ms(64999)); // 1 回だけ残る（5 秒の 1 回）。0 回になるまで戻らない
	CHECK(m.state() == EngineState::kLagging);
	m.on_tick(ms(65001)); // 0 回
	CHECK(m.state() == EngineState::kRunningGpu);
}

TEST_CASE("TC-15 同じログは 60 秒に 1 回まで")
{
	LogLimiter limiter;
	CHECK(limiter.allow("late", ms(0)));
	CHECK_FALSE(limiter.allow("late", ms(59999)));
	CHECK(limiter.allow("other", ms(59999)));
	CHECK(limiter.allow("late", ms(60000)));
}
