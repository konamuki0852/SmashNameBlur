#include "plugin/obs_frame.hpp"

#include <media-io/video-io.h>

#include <algorithm>

namespace omask::plugin {

core::PixelFormat to_pixel_format(enum video_format format)
{
	switch (format) {
	case VIDEO_FORMAT_NV12:
		return core::PixelFormat::kNv12;
	case VIDEO_FORMAT_I420:
		return core::PixelFormat::kI420;
	case VIDEO_FORMAT_I422:
		return core::PixelFormat::kI422;
	case VIDEO_FORMAT_I444:
		return core::PixelFormat::kI444;
	case VIDEO_FORMAT_YUY2:
		return core::PixelFormat::kYuy2;
	case VIDEO_FORMAT_YVYU:
		return core::PixelFormat::kYvyu;
	case VIDEO_FORMAT_UYVY:
		return core::PixelFormat::kUyvy;
	case VIDEO_FORMAT_RGBA:
		return core::PixelFormat::kRgba;
	case VIDEO_FORMAT_BGRA:
		return core::PixelFormat::kBgra;
	case VIDEO_FORMAT_BGRX:
		return core::PixelFormat::kBgrx;
	default:
		return core::PixelFormat::kUnsupported;
	}
}

core::FrameView to_raw_view(const obs_source_frame &frame)
{
	core::FrameView view;
	view.format = to_pixel_format(frame.format);
	view.width = static_cast<int>(frame.width);
	view.height = static_cast<int>(frame.height);
	for (size_t i = 0; i < view.data.size(); ++i) {
		view.data[i] = frame.data[i];
		view.linesize[i] = static_cast<int>(frame.linesize[i]);
	}
	view.full_range = frame.full_range;
	std::copy(std::begin(frame.color_matrix), std::end(frame.color_matrix), view.color_matrix.begin());
	std::copy(std::begin(frame.color_range_min), std::end(frame.color_range_min), view.color_range_min.begin());
	std::copy(std::begin(frame.color_range_max), std::end(frame.color_range_max), view.color_range_max.begin());
	return view;
}

core::FrameView to_frame_view(const obs_source_frame &frame)
{
	core::FrameView view = to_raw_view(frame);
	if (frame.flip)
		core::flip_vertically(view);
	return view;
}

const char *format_name(enum video_format format)
{
	return get_video_format_name(format);
}

} // namespace omask::plugin
