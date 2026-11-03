// モザイク・塗りつぶし・確認用の枠（PRC-06）。テスト設計書 TC-07・08
#include "doctest/doctest.h"

#include <array>
#include <string>

#include "core/draw.hpp"
#include "frame_builder.hpp"

using omask::core::align_even;
using omask::core::Component;
using omask::core::component_height;
using omask::core::component_layout;
using omask::core::component_width;
using omask::core::draw_regions;
using omask::core::DrawStyle;
using omask::core::is_chroma_subsampled;
using omask::core::MaskRegion;
using omask::core::mosaic_block_size;
using omask::core::PixelFormat;
using omask::core::RectI;
using omask::core::RegionKind;
using omask::core::sample_ptr;
using omask::core::StyleKind;
using omask::test::FrameBuilder;

namespace {

const std::array<std::pair<PixelFormat, const char *>, 10> kFormats{{
	{PixelFormat::kNv12, "NV12"},
	{PixelFormat::kI420, "I420"},
	{PixelFormat::kI422, "I422"},
	{PixelFormat::kI444, "I444"},
	{PixelFormat::kYuy2, "YUY2"},
	{PixelFormat::kYvyu, "YVYU"},
	{PixelFormat::kUyvy, "UYVY"},
	{PixelFormat::kRgba, "RGBA"},
	{PixelFormat::kBgra, "BGRA"},
	{PixelFormat::kBgrx, "BGRX"},
}};

RectI to_component(const RectI &rect, const Component &c)
{
	return RectI{rect.x0 / c.sub_x, rect.y0 / c.sub_y, (rect.x1 + c.sub_x - 1) / c.sub_x,
		     (rect.y1 + c.sub_y - 1) / c.sub_y};
}

bool inside(const RectI &rect, int x, int y)
{
	return x >= rect.x0 && x < rect.x1 && y >= rect.y0 && y < rect.y1;
}

} // namespace

TEST_CASE("TC-07 形式ごとの成分の並び")
{
	const auto yuy2 = component_layout(PixelFormat::kYuy2);
	CHECK((yuy2.components[0].step == 2 && yuy2.components[0].offset == 0)); // Y0 Y1
	CHECK((yuy2.components[1].step == 4 && yuy2.components[1].offset == 1)); // U
	CHECK((yuy2.components[2].step == 4 && yuy2.components[2].offset == 3)); // V
	const auto uyvy = component_layout(PixelFormat::kUyvy);
	CHECK((uyvy.components[0].offset == 1 && uyvy.components[1].offset == 0 && uyvy.components[2].offset == 2));
	const auto yvyu = component_layout(PixelFormat::kYvyu);
	CHECK((yvyu.components[1].offset == 3 && yvyu.components[2].offset == 1)); // U は 3、V は 1
	const auto nv12 = component_layout(PixelFormat::kNv12);
	CHECK((nv12.components[1].plane == 1 && nv12.components[1].step == 2 && nv12.components[2].offset == 1));
	const auto bgra = component_layout(PixelFormat::kBgra);
	CHECK((bgra.components[0].offset == 2 && bgra.components[2].offset == 0)); // R は 2、B は 0
}

TEST_CASE("TC-07 モザイク: 外側は 1 バイトも変わらず、内側は塊の元の平均（全 10 形式）")
{
	const int width = 384;
	const int height = 216;
	const RectI region{101, 51, 301, 151}; // 奇数の座標（色差が間引かれた形式では偶数に広がる）
	for (const auto &[format, name] : kFormats) {
		CAPTURE(std::string(name));
		FrameBuilder builder(format, width, height);
		const auto original = builder.snapshot();
		DrawStyle style;
		style.mosaic_block_px = 24; // 丸めと偶数へのそろえを見るため、割り切れない大きさにする
		draw_regions(builder.view(), {MaskRegion{region, RegionKind::kFixed}}, style);

		const bool subsampled = is_chroma_subsampled(format);
		const RectI painted = subsampled ? align_even(region, width, height) : region;
		if (subsampled) {
			CHECK(painted == RectI{100, 50, 302, 152});
		}
		const int block = mosaic_block_size(width, style.mosaic_block_px, subsampled);
		CHECK(block == (subsampled ? 6 : 5)); // 24 x 384 / 1920 = 4.8 → 5（偶数にそろえると 6）
		const auto layout = component_layout(format);
		int changed_outside = 0;
		int wrong_inside = 0;
		for (int i = 0; i < layout.count; ++i) {
			const Component &c = layout.components[i];
			const RectI area = to_component(painted, c);
			const int bw = block / c.sub_x;
			const int bh = block / c.sub_y;
			const int cw = component_width(builder.view(), c);
			const int ch = component_height(builder.view(), c);
			for (int y = 0; y < ch; ++y) {
				for (int x = 0; x < cw; ++x) {
					const uint8_t now = *sample_ptr(builder.view(), c, x, y);
					if (!inside(area, x, y)) {
						changed_outside += now != builder.original(original, c, x, y);
						continue;
					}
					// 塊の格子はコマ全体の塊の大きさの倍数
					const int x0 = x / bw * bw;
					const int y0 = y / bh * bh;
					uint64_t sum = 0;
					uint64_t count = 0;
					for (int yy = y0; yy < std::min(y0 + bh, ch); ++yy) {
						for (int xx = x0; xx < std::min(x0 + bw, cw); ++xx) {
							sum += builder.original(original, c, xx, yy);
							++count;
						}
					}
					wrong_inside += now != static_cast<uint8_t>((sum + count / 2) / count);
				}
			}
		}
		CHECK(changed_outside == 0);
		CHECK(wrong_inside == 0);
	}
}

TEST_CASE("TC-07 範囲がコマの外なら何もしない・対応外の形式なら何もしない")
{
	FrameBuilder builder(PixelFormat::kNv12, 64, 32);
	const auto original = builder.snapshot();
	draw_regions(builder.view(), {MaskRegion{RectI{100, 100, 200, 200}, RegionKind::kTag}}, DrawStyle{});
	CHECK(builder.snapshot() == original);

	FrameBuilder other(PixelFormat::kUnsupported, 64, 32);
	const auto before = other.snapshot();
	draw_regions(other.view(), {MaskRegion{RectI{0, 0, 32, 16}, RegionKind::kTag}}, DrawStyle{});
	CHECK(other.snapshot() == before);
}

TEST_CASE("TC-08 塗りつぶし: YUV は BT.709・限定範囲の値と ±1 で一致する")
{
	DrawStyle style;
	style.kind = StyleKind::kFill;
	SUBCASE("黒")
	{
		FrameBuilder builder(PixelFormat::kNv12, 64, 32);
		style.fill_color = 0xFF000000u;
		draw_regions(builder.view(), {MaskRegion{RectI{8, 8, 24, 24}, RegionKind::kFixed}}, style);
		const auto layout = component_layout(PixelFormat::kNv12);
		CHECK(*sample_ptr(builder.view(), layout.components[0], 10, 10) == 16);
		CHECK(*sample_ptr(builder.view(), layout.components[1], 5, 5) == 128);
		CHECK(*sample_ptr(builder.view(), layout.components[2], 5, 5) == 128);
	}
	SUBCASE("赤（R=255）")
	{
		FrameBuilder builder(PixelFormat::kI420, 64, 32);
		style.fill_color = 0xFF0000FFu; // 0xAABBGGRR
		draw_regions(builder.view(), {MaskRegion{RectI{8, 8, 24, 24}, RegionKind::kFixed}}, style);
		const auto layout = component_layout(PixelFormat::kI420);
		CHECK(std::abs(*sample_ptr(builder.view(), layout.components[0], 10, 10) - 63) <= 1);
		CHECK(std::abs(*sample_ptr(builder.view(), layout.components[1], 5, 5) - 102) <= 1);
		CHECK(std::abs(*sample_ptr(builder.view(), layout.components[2], 5, 5) - 240) <= 1);
	}
	SUBCASE("RGB の形式はそのまま塗り、不透明にする")
	{
		FrameBuilder builder(PixelFormat::kBgra, 64, 32);
		style.fill_color = 0xFF0000FFu;
		draw_regions(builder.view(), {MaskRegion{RectI{8, 8, 24, 24}, RegionKind::kFixed}}, style);
		const uint8_t *p = builder.view().data[0] + 10 * builder.view().linesize[0] + 10 * 4;
		CHECK(p[0] == 0);   // B
		CHECK(p[1] == 0);   // G
		CHECK(p[2] == 255); // R
		CHECK(p[3] == 255); // A
	}
}

TEST_CASE("TC-08 確認用の枠: モザイクの後に外側へ描き、色は範囲の種類で違い、内側はモザイクのまま")
{
	const RectI fixed_rect{40, 40, 100, 80};
	const RectI tag_rect{200, 100, 260, 140};
	FrameBuilder plain(PixelFormat::kBgra, 384, 216);
	FrameBuilder overlay(PixelFormat::kBgra, 384, 216);
	const std::vector<MaskRegion> regions{MaskRegion{fixed_rect, RegionKind::kFixed},
					      MaskRegion{tag_rect, RegionKind::kTag}};
	DrawStyle style;
	draw_regions(plain.view(), regions, style);
	style.debug_overlay = true;
	draw_regions(overlay.view(), regions, style);

	auto pixel = [](FrameBuilder &b, int x, int y) {
		const uint8_t *p = b.view().data[0] + y * b.view().linesize[0] + x * 4;
		return std::array<uint8_t, 3>{p[2], p[1], p[0]}; // R G B
	};
	// 内側はモザイクだけを描いたときと同じ
	CHECK(pixel(overlay, 50, 50) == pixel(plain, 50, 50));
	CHECK(pixel(overlay, 210, 110) == pixel(plain, 210, 110));
	// 外側のすぐ上に枠（位置の決まった範囲は黄、頭上タグは水色）
	CHECK(pixel(overlay, 60, 39) == std::array<uint8_t, 3>{255, 255, 0});
	CHECK(pixel(overlay, 220, 99) == std::array<uint8_t, 3>{0, 255, 255});
}
