#include "core/draw.hpp"

#include <algorithm>
#include <array>

#include "core/color.hpp"
#include "core/geometry.hpp"
#include "core/params.hpp"

namespace omask::core {

namespace {

constexpr int kOverlayThicknessPx = 2; // 確認用の枠の太さ（1080p 換算）

// 範囲（画素）を成分の標本の座標に直す（間引きのある成分では外側へ丸める）
RectI to_component(const RectI &rect, const Component &c)
{
	return RectI{rect.x0 / c.sub_x, rect.y0 / c.sub_y, (rect.x1 + c.sub_x - 1) / c.sub_x,
		     (rect.y1 + c.sub_y - 1) / c.sub_y};
}

RectI intersect(const RectI &a, const RectI &b)
{
	return RectI{std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
}

void fill_samples(FrameView &frame, const Component &c, const RectI &area, uint8_t value)
{
	for (int y = area.y0; y < area.y1; ++y) {
		uint8_t *p = sample_ptr(frame, c, area.x0, y);
		if (c.step == 1) {
			std::fill(p, p + area.width(), value);
			continue;
		}
		for (int x = area.x0; x < area.x1; ++x, p += c.step)
			*p = value;
	}
}

uint64_t sum_samples(const FrameView &frame, const Component &c, const RectI &area)
{
	uint64_t sum = 0;
	for (int y = area.y0; y < area.y1; ++y) {
		const uint8_t *p = sample_ptr(frame, c, area.x0, y);
		uint32_t row = 0;
		for (int x = area.x0; x < area.x1; ++x, p += c.step)
			row += *p;
		sum += row;
	}
	return sum;
}

// 塗る範囲を、形式に合わせて切り詰め・偶数にそろえる
std::vector<RectI> prepare_rects(const FrameView &frame, const std::vector<MaskRegion> &regions)
{
	const bool subsampled = is_chroma_subsampled(frame.format);
	std::vector<RectI> rects;
	for (const MaskRegion &region : regions) {
		RectI rect = clip_rect(region.rect, frame.width, frame.height);
		if (subsampled)
			rect = align_even(rect, frame.width, frame.height);
		if (!rect.empty())
			rects.push_back(rect);
	}
	return rects;
}

void apply_mosaic(FrameView &frame, const std::vector<RectI> &rects, int block)
{
	const int grid_w = (frame.width + block - 1) / block;
	const int grid_h = (frame.height + block - 1) / block;
	std::vector<uint8_t> touched(static_cast<size_t>(grid_w) * grid_h, 0);
	for (const RectI &rect : rects) {
		for (int by = rect.y0 / block; by <= (rect.y1 - 1) / block; ++by) {
			for (int bx = rect.x0 / block; bx <= (rect.x1 - 1) / block; ++bx)
				touched[static_cast<size_t>(by) * grid_w + bx] = 1;
		}
	}

	const ComponentLayout layout = component_layout(frame.format);
	for (int i = 0; i < layout.count; ++i) {
		const Component &c = layout.components[i];
		const RectI plane{0, 0, component_width(frame, c), component_height(frame, c)};
		const int block_w = block / c.sub_x;
		const int block_h = block / c.sub_y;
		for (int by = 0; by < grid_h; ++by) {
			for (int bx = 0; bx < grid_w; ++bx) {
				if (!touched[static_cast<size_t>(by) * grid_w + bx])
					continue;
				const RectI cell = intersect(RectI{bx * block_w, by * block_h, (bx + 1) * block_w,
								   (by + 1) * block_h},
							     plane);
				if (cell.empty())
					continue;
				// 塊の中の元の画素の平均（塊は重ならないので、塗る前の値を読める）
				const uint64_t sum = sum_samples(frame, c, cell);
				const uint64_t count = static_cast<uint64_t>(cell.width()) * cell.height();
				const auto average = static_cast<uint8_t>((sum + count / 2) / count);
				for (const RectI &rect : rects) {
					const RectI area = intersect(cell, to_component(rect, c));
					if (!area.empty())
						fill_samples(frame, c, area, average);
				}
			}
		}
	}
}

// 色（RGB 0〜1）を成分ごとの 8 bit の値にする
std::array<uint8_t, 4> component_values(const FrameView &frame, float r, float g, float b)
{
	if (is_yuv(frame.format)) {
		const auto yuv = rgb_to_yuv_bytes(frame, r, g, b);
		return {yuv[0], yuv[1], yuv[2], 0};
	}
	auto to_byte = [](float value) {
		return static_cast<uint8_t>(std::clamp(static_cast<int>(value * 255.0f + 0.5f), 0, 255));
	};
	return {to_byte(r), to_byte(g), to_byte(b), 255};
}

void apply_color(FrameView &frame, const std::vector<RectI> &rects, const std::array<uint8_t, 4> &values)
{
	const ComponentLayout layout = component_layout(frame.format);
	for (int i = 0; i < layout.count; ++i) {
		const Component &c = layout.components[i];
		const RectI plane{0, 0, component_width(frame, c), component_height(frame, c)};
		for (const RectI &rect : rects)
			fill_samples(frame, c, intersect(to_component(rect, c), plane), values[i]);
	}
}

// 範囲の外側に枠を描く（モザイクの後に描くので、名前は隠れたまま）
void draw_overlay(FrameView &frame, const std::vector<MaskRegion> &regions)
{
	const bool subsampled = is_chroma_subsampled(frame.format);
	int t = (kOverlayThicknessPx * frame.width + kReferenceWidth / 2) / kReferenceWidth;
	t = std::max(t, 2);
	if (subsampled && t % 2 != 0)
		++t;
	for (const MaskRegion &region : regions) {
		const RectI r = clip_rect(region.rect, frame.width, frame.height);
		if (r.empty())
			continue;
		const std::vector<MaskRegion> edges{
			MaskRegion{RectI{r.x0 - t, r.y0 - t, r.x1 + t, r.y0}, region.kind},
			MaskRegion{RectI{r.x0 - t, r.y1, r.x1 + t, r.y1 + t}, region.kind},
			MaskRegion{RectI{r.x0 - t, r.y0, r.x0, r.y1}, region.kind},
			MaskRegion{RectI{r.x1, r.y0, r.x1 + t, r.y1}, region.kind},
		};
		// 位置の決まった範囲は黄、頭上タグは水色
		const bool fixed = region.kind == RegionKind::kFixed;
		apply_color(frame, prepare_rects(frame, edges),
			    component_values(frame, fixed ? 1.0f : 0.0f, 1.0f, fixed ? 0.0f : 1.0f));
	}
}

} // namespace

int mosaic_block_size(int frame_width, int mosaic_block_px, bool chroma_subsampled)
{
	int block = (mosaic_block_px * frame_width + kReferenceWidth / 2) / kReferenceWidth;
	block = std::max(block, 4);
	if (chroma_subsampled && block % 2 != 0)
		++block;
	return block;
}

RectI align_even(const RectI &rect, int width, int height)
{
	const RectI even{rect.x0 & ~1, rect.y0 & ~1, (rect.x1 + 1) & ~1, (rect.y1 + 1) & ~1};
	return clip_rect(even, width, height);
}

void draw_regions(FrameView &frame, const std::vector<MaskRegion> &regions, const DrawStyle &style)
{
	if (!is_supported(frame.format) || frame.width <= 0 || frame.height <= 0 || regions.empty())
		return;
	const std::vector<RectI> rects = prepare_rects(frame, regions);
	if (style.kind == StyleKind::kMosaic) {
		apply_mosaic(frame, rects,
			     mosaic_block_size(frame.width, style.mosaic_block_px, is_chroma_subsampled(frame.format)));
	} else {
		const uint32_t c = style.fill_color;
		apply_color(frame, rects,
			    component_values(frame, static_cast<float>(c & 0xFF) / 255.0f,
					     static_cast<float>((c >> 8) & 0xFF) / 255.0f,
					     static_cast<float>((c >> 16) & 0xFF) / 255.0f));
	}
	if (style.debug_overlay)
		draw_overlay(frame, regions);
}

} // namespace omask::core
