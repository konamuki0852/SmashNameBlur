// コマの平面の並びと上下の反転（OBS のコマを写す・flip の立ったコマを扱う。PRC-01 手順 5、付録 A）。テスト設計書 TC-07・09
#include "doctest/doctest.h"

#include <cstring>
#include <vector>

#include "core/draw.hpp"
#include "core/frame.hpp"
#include "core/preprocess.hpp"
#include "frame_builder.hpp"

using omask::core::DrawStyle;
using omask::core::FrameView;
using omask::core::MaskRegion;
using omask::core::PixelFormat;
using omask::core::RectI;
using omask::core::RegionKind;
using omask::test::FrameBuilder;

namespace {

const PixelFormat kAllFormats[] = {PixelFormat::kNv12, PixelFormat::kI420, PixelFormat::kI422, PixelFormat::kI444,
				   PixelFormat::kYuy2, PixelFormat::kYvyu, PixelFormat::kUyvy, PixelFormat::kRgba,
				   PixelFormat::kBgra, PixelFormat::kBgrx};

// a の各平面の行を上下逆にして b に写す（b は a と同じ形式・大きさ・1 行のバイト数）
void copy_rows_reversed(const FrameView &a, FrameView &b)
{
	for (int p = 0; p < omask::core::plane_count(a.format); ++p) {
		const size_t i = static_cast<size_t>(p);
		const int rows = omask::core::plane_rows(a, p);
		for (int y = 0; y < rows; ++y)
			std::memcpy(b.data[i] + static_cast<int64_t>(y) * b.linesize[i],
				    a.data[i] + static_cast<int64_t>(rows - 1 - y) * a.linesize[i],
				    static_cast<size_t>(a.linesize[i]));
	}
}

// view（上下を逆にした見え方でもよい）の行 y と、b の行 y が同じか
bool same_rows(const FrameView &view, const FrameView &b)
{
	for (int p = 0; p < omask::core::plane_count(b.format); ++p) {
		const size_t i = static_cast<size_t>(p);
		const int rows = omask::core::plane_rows(b, p);
		const int bytes = b.linesize[i] < 0 ? -b.linesize[i] : b.linesize[i];
		for (int y = 0; y < rows; ++y) {
			if (std::memcmp(view.data[i] + static_cast<int64_t>(y) * view.linesize[i],
					b.data[i] + static_cast<int64_t>(y) * b.linesize[i],
					static_cast<size_t>(bytes)) != 0)
				return false;
		}
	}
	return true;
}

} // namespace

TEST_CASE("TC-07 平面の数と行の数（高さが奇数なら色差の行は切り上げ）")
{
	struct Case {
		PixelFormat format;
		int planes;
		int rows[3];
	};
	const Case cases[] = {
		{PixelFormat::kNv12, 2, {1081, 541, 0}},     {PixelFormat::kI420, 3, {1081, 541, 541}},
		{PixelFormat::kI422, 3, {1081, 1081, 1081}}, {PixelFormat::kI444, 3, {1081, 1081, 1081}},
		{PixelFormat::kYuy2, 1, {1081, 0, 0}},       {PixelFormat::kUyvy, 1, {1081, 0, 0}},
		{PixelFormat::kBgra, 1, {1081, 0, 0}},       {PixelFormat::kUnsupported, 0, {0, 0, 0}},
	};
	for (const Case &c : cases) {
		FrameView frame;
		frame.format = c.format;
		frame.width = 1921;
		frame.height = 1081;
		CAPTURE(static_cast<int>(c.format));
		CHECK(omask::core::plane_count(c.format) == c.planes);
		for (int p = 0; p < 3; ++p)
			CHECK(omask::core::plane_rows(frame, p) == c.rows[p]);
	}
}

TEST_CASE("TC-07 上下を逆にした見え方への描き込みは、上下を並べ直したコマへの描き込みと同じ（全 10 形式）")
{
	const std::vector<MaskRegion> regions{{RectI{20, 10, 90, 40}, RegionKind::kFixed},
					      {RectI{130, 90, 210, 140}, RegionKind::kTag}};
	DrawStyle style;
	style.mosaic_block_px = 16;
	style.debug_overlay = true;
	for (PixelFormat format : kAllFormats) {
		CAPTURE(static_cast<int>(format));
		FrameBuilder stored(format, 256, 144, 7);   // 下の行から並んだコマ（flip が立ったもの）
		FrameBuilder upright(format, 256, 144, 99); // 上の行から並べ直したもの
		copy_rows_reversed(stored.view(), upright.view());
		FrameView flipped = stored.view();
		omask::core::flip_vertically(flipped);
		REQUIRE(same_rows(flipped, upright.view()));

		omask::core::draw_regions(flipped, regions, style);
		omask::core::draw_regions(upright.view(), regions, style);
		CHECK(same_rows(flipped, upright.view()));
	}
}

TEST_CASE("TC-09 上下を逆にした見え方の前処理は、上下を並べ直したコマの前処理と同じ")
{
	for (PixelFormat format : {PixelFormat::kNv12, PixelFormat::kYuy2, PixelFormat::kBgra}) {
		CAPTURE(static_cast<int>(format));
		FrameBuilder stored(format, 1280, 720, 3);
		FrameBuilder upright(format, 1280, 720, 4);
		copy_rows_reversed(stored.view(), upright.view());
		FrameView flipped = stored.view();
		omask::core::flip_vertically(flipped);

		std::vector<float> a(omask::core::kTensorSize);
		std::vector<float> b(omask::core::kTensorSize);
		const float brightness_a = omask::core::preprocess_frame(flipped, a.data());
		const float brightness_b = omask::core::preprocess_frame(upright.view(), b.data());
		CHECK(brightness_a == brightness_b);
		CHECK(a == b);
	}
}
