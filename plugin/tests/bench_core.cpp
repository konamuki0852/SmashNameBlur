// 所要時間の目安（ふだんは飛ばす。omask_tests --no-skip -ts=bench で実行する）。非機能・運用設計書 §5.1
#include "doctest/doctest.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "core/draw.hpp"
#include "core/fixed_policy.hpp"
#include "core/preprocess.hpp"
#include "frame_builder.hpp"

using omask::core::FixedActive;
using omask::core::MaskRegion;
using omask::core::PixelFormat;
using omask::core::RectI;
using omask::core::RegionKind;
using omask::test::FrameBuilder;

namespace {

template<class Fn> double median_ms(Fn &&fn, int runs = 31)
{
	std::vector<double> times;
	for (int i = 0; i < runs; ++i) {
		const auto t0 = std::chrono::steady_clock::now();
		fn();
		const auto t1 = std::chrono::steady_clock::now();
		times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
	}
	std::sort(times.begin(), times.end());
	return times[times.size() / 2];
}

} // namespace

TEST_SUITE("bench" * doctest::skip())
{
	TEST_CASE("前処理と描き込みの所要時間（1080p）")
	{
		for (PixelFormat format : {PixelFormat::kNv12, PixelFormat::kYuy2, PixelFormat::kBgra}) {
			FrameBuilder builder(format, 1920, 1080);
			std::vector<float> tensor(omask::core::kTensorSize);
			const double pre =
				median_ms([&] { omask::core::preprocess_frame(builder.view(), tensor.data()); });
			std::vector<MaskRegion> regions = omask::core::fixed_regions(
				FixedActive::all(), omask::core::default_fixed_rects(), 1920, 1080);
			for (int i = 0; i < 5; ++i)
				regions.push_back(
					MaskRegion{RectI{200 + i * 300, 300, 530 + i * 300, 396}, RegionKind::kTag});
			const double draw = median_ms(
				[&] { omask::core::draw_regions(builder.view(), regions, omask::core::DrawStyle{}); });
			std::printf("format %d: preprocess %.2f ms, draw (3 fixed + 5 tags) %.3f ms\n",
				    static_cast<int>(format), pre, draw);
			CHECK(pre > 0.0);
		}
	}
}
