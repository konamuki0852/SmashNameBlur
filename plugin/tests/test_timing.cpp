// 所要時間の集計（PRC-09 手順 3）。テスト設計書 TC-15
#include "doctest/doctest.h"

#include "core/timing.hpp"

using omask::core::TimingSummary;
using omask::core::TimingWindow;

TEST_CASE("TC-15 所要時間の集計: 平均・99%・最大を出し、集計したら空になる")
{
	TimingWindow window;
	for (int i = 100; i >= 1; --i)
		window.add(static_cast<double>(i));
	const TimingSummary summary = window.take();
	CHECK(summary.count == 100);
	CHECK(summary.average_ms == doctest::Approx(50.5));
	CHECK(summary.p99_ms == 99.0);
	CHECK(summary.max_ms == 100.0);
	CHECK(window.take().count == 0);
}

TEST_CASE("TC-15 所要時間の集計: 1 つだけなら平均も 99% もその値。上限を超えた分は数えない")
{
	TimingWindow window;
	window.add(2.5);
	const TimingSummary one = window.take();
	CHECK(one.count == 1);
	CHECK(one.p99_ms == 2.5);
	for (std::size_t i = 0; i < TimingWindow::kMaxSamples + 10; ++i)
		window.add(1.0);
	CHECK(window.take().count == TimingWindow::kMaxSamples);
}
