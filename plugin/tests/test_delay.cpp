// 遅延 D の計算と遅延バッファ（PRC-01）。テスト設計書 TC-13・14
#include "doctest/doctest.h"

#include <vector>

#include "core/delay.hpp"
#include "core/delay_buffer.hpp"
#include "test_helpers.hpp"

using omask::core::DelayBuffer;
using omask::core::DelayEstimator;
using omask::test::ms;

TEST_CASE("TC-13 遅延 D: 60fps で約 133 ms、30fps で約 167 ms")
{
	SUBCASE("最初（まだ間隔が分からない）は 60fps とみなす")
	{
		DelayEstimator e;
		CHECK(e.delay_ns(100, 2) == ms(100) + 2 * 16666667);
	}
	SUBCASE("60fps")
	{
		DelayEstimator e;
		for (int i = 0; i < 40; ++i)
			e.add_timestamp(static_cast<int64_t>(i) * 16666667);
		CHECK(static_cast<double>(e.delay_ns(100, 2)) == doctest::Approx(133.3e6).epsilon(0.001));
	}
	SUBCASE("30fps")
	{
		DelayEstimator e;
		for (int i = 0; i < 40; ++i)
			e.add_timestamp(static_cast<int64_t>(i) * 33333333);
		CHECK(static_cast<double>(e.delay_ns(100, 2)) == doctest::Approx(166.7e6).epsilon(0.001));
	}
	SUBCASE("揺れがあっても中央値で安定する")
	{
		DelayEstimator e;
		int64_t t = 0;
		for (int i = 0; i < 40; ++i) {
			const int64_t jitter = (i % 5 == 0) ? 9000000 : (i % 3 == 0 ? -2000000 : 500000);
			t += 16666667 + jitter;
			e.add_timestamp(t);
		}
		CHECK(e.frame_interval_ns() == 16666667 + 500000);
	}
}

namespace {

struct FakeFrame {
	int id;
};

struct Recorder {
	std::vector<int> released;
	void operator()(FakeFrame f) { released.push_back(f.id); }
};

constexpr int64_t kInterval = 16666667;
constexpr int64_t kDelay = 100000000 + 2 * kInterval; // 約 133 ms

} // namespace

TEST_CASE("TC-13 1 秒を超える間隔（時刻の飛び）はコマの間隔に数えない（遅延が何秒にもならない）")
{
	DelayEstimator e;
	e.add_timestamp(0);
	e.add_timestamp(ms(5000)); // フィルターの無効 → 有効などの飛び
	CHECK(e.frame_interval_ns() == DelayEstimator::kDefaultIntervalNs);
	e.add_timestamp(ms(5000) + 33333333);
	CHECK(e.frame_interval_ns() == 33333333);
}

TEST_CASE("TC-14 遅延バッファ: 満ちるまで返さず、以後は 1 つ入れて 1 つ返す")
{
	DelayBuffer<FakeFrame> buffer;
	Recorder recorder;
	int outputs = 0;
	for (int i = 0; i < 8; ++i) { // 0〜7 番目: 時刻の差が 133 ms に届くのは 8 番目（i=8, 133.3 ms）
		auto r = buffer.push(FakeFrame{i}, i * kInterval, kDelay, recorder);
		CHECK_FALSE(r.output.has_value());
		outputs += r.output.has_value();
	}
	auto r8 = buffer.push(FakeFrame{8}, 8 * kInterval, kDelay, recorder);
	REQUIRE(r8.output.has_value());
	CHECK(r8.output->id == 0);
	for (int i = 9; i < 20; ++i) {
		auto r = buffer.push(FakeFrame{i}, i * kInterval, kDelay, recorder);
		REQUIRE(r.output.has_value());
		CHECK(r.output->id == i - 8);
	}
	CHECK(buffer.size() == 8);
	CHECK(recorder.released.empty());
}

TEST_CASE("TC-14 時刻が戻る・1 秒を超えて進むと、溜めたコマをすべて返してやり直す")
{
	for (int64_t jump : {-ms(10), ms(1001)}) {
		DelayBuffer<FakeFrame> buffer;
		Recorder recorder;
		for (int i = 0; i < 12; ++i)
			buffer.push(FakeFrame{i}, ms(1000) + i * kInterval, kDelay, recorder);
		const int64_t last = ms(1000) + 11 * kInterval;
		auto r = buffer.push(FakeFrame{100}, last + jump, kDelay, recorder);
		CHECK(r.time_jump);
		CHECK_FALSE(r.output.has_value());    // やり直したので、また満ちるまで返さない
		CHECK(recorder.released.size() == 8); // 溜めていた 8 つ（4〜11 番目）
		CHECK(buffer.size() == 1);
	}
}

TEST_CASE("TC-14 1 秒ちょうどの進みは飛びとみなさない")
{
	DelayBuffer<FakeFrame> buffer;
	Recorder recorder;
	buffer.push(FakeFrame{0}, ms(1000), kDelay, recorder);
	auto r = buffer.push(FakeFrame{1}, ms(2000), kDelay, recorder);
	CHECK_FALSE(r.time_jump);
}

TEST_CASE("TC-14 外す・破棄するときは、溜めたコマをすべて返す")
{
	DelayBuffer<FakeFrame> buffer;
	Recorder recorder;
	for (int i = 0; i < 5; ++i)
		buffer.push(FakeFrame{i}, i * kInterval, kDelay, recorder);
	buffer.release_all(recorder);
	CHECK(recorder.released == std::vector<int>{0, 1, 2, 3, 4});
	CHECK(buffer.size() == 0);
	CHECK_FALSE(buffer.reached());
}
