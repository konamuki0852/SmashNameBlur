#include "core/frame.hpp"

#include <algorithm>
#include <cstddef>

namespace omask::core {

bool is_supported(PixelFormat format)
{
	return format != PixelFormat::kUnsupported;
}

bool is_yuv(PixelFormat format)
{
	switch (format) {
	case PixelFormat::kNv12:
	case PixelFormat::kI420:
	case PixelFormat::kI422:
	case PixelFormat::kI444:
	case PixelFormat::kYuy2:
	case PixelFormat::kYvyu:
	case PixelFormat::kUyvy:
		return true;
	default:
		return false;
	}
}

bool is_chroma_subsampled(PixelFormat format)
{
	return is_yuv(format) && format != PixelFormat::kI444;
}

ComponentLayout component_layout(PixelFormat format)
{
	// Component: 平面・横の間引き・縦の間引き・隣の標本までのバイト数・位置
	switch (format) {
	case PixelFormat::kNv12:
		return {3, {Component{0, 1, 1, 1, 0}, Component{1, 2, 2, 2, 0}, Component{1, 2, 2, 2, 1}}};
	case PixelFormat::kI420:
		return {3, {Component{0, 1, 1, 1, 0}, Component{1, 2, 2, 1, 0}, Component{2, 2, 2, 1, 0}}};
	case PixelFormat::kI422:
		return {3, {Component{0, 1, 1, 1, 0}, Component{1, 2, 1, 1, 0}, Component{2, 2, 1, 1, 0}}};
	case PixelFormat::kI444:
		return {3, {Component{0, 1, 1, 1, 0}, Component{1, 1, 1, 1, 0}, Component{2, 1, 1, 1, 0}}};
	case PixelFormat::kYuy2: // Y0 U Y1 V
		return {3, {Component{0, 1, 1, 2, 0}, Component{0, 2, 1, 4, 1}, Component{0, 2, 1, 4, 3}}};
	case PixelFormat::kYvyu: // Y0 V Y1 U
		return {3, {Component{0, 1, 1, 2, 0}, Component{0, 2, 1, 4, 3}, Component{0, 2, 1, 4, 1}}};
	case PixelFormat::kUyvy: // U Y0 V Y1
		return {3, {Component{0, 1, 1, 2, 1}, Component{0, 2, 1, 4, 0}, Component{0, 2, 1, 4, 2}}};
	case PixelFormat::kRgba:
		return {4,
			{Component{0, 1, 1, 4, 0}, Component{0, 1, 1, 4, 1}, Component{0, 1, 1, 4, 2},
			 Component{0, 1, 1, 4, 3}}};
	case PixelFormat::kBgra:
	case PixelFormat::kBgrx:
		return {4,
			{Component{0, 1, 1, 4, 2}, Component{0, 1, 1, 4, 1}, Component{0, 1, 1, 4, 0},
			 Component{0, 1, 1, 4, 3}}};
	case PixelFormat::kUnsupported:
		break;
	}
	return {};
}

int plane_count(PixelFormat format)
{
	const ComponentLayout layout = component_layout(format);
	int count = 0;
	for (int i = 0; i < layout.count; ++i)
		count = std::max(count, layout.components[static_cast<size_t>(i)].plane + 1);
	return count;
}

int plane_rows(const FrameView &frame, int plane)
{
	const ComponentLayout layout = component_layout(frame.format);
	int rows = 0;
	for (int i = 0; i < layout.count; ++i) {
		const Component &c = layout.components[static_cast<size_t>(i)];
		if (c.plane == plane)
			rows = std::max(rows, component_height(frame, c));
	}
	return rows;
}

void flip_vertically(FrameView &frame)
{
	const int count = plane_count(frame.format);
	for (int p = 0; p < count; ++p) {
		const size_t i = static_cast<size_t>(p);
		const int rows = plane_rows(frame, p);
		if (rows > 0)
			frame.data[i] += static_cast<int64_t>(rows - 1) * frame.linesize[i];
		frame.linesize[i] = -frame.linesize[i];
	}
}

} // namespace omask::core
