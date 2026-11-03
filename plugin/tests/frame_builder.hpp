// テスト用の合成のコマ（形式ごとに平面を確保し、決まった乱数で埋める）
#pragma once

#include <cstdint>
#include <vector>

#include "core/color.hpp"
#include "core/frame.hpp"

namespace omask::test {

class FrameBuilder {
public:
	FrameBuilder(core::PixelFormat format, int width, int height, uint32_t seed = 1) : format_(format)
	{
		view_.format = format;
		view_.width = width;
		view_.height = height;
		view_.color_matrix = core::kBt709LimitedMatrix;
		view_.color_range_min = core::kBt709LimitedMin;
		view_.color_range_max = core::kBt709LimitedMax;
		const int pad = 8; // 1 行のバイト数が幅と違っても正しく扱うことを確かめる
		auto add_plane = [&](int row_bytes, int rows) {
			const int linesize = row_bytes + pad;
			planes_.emplace_back(static_cast<size_t>(linesize) * rows);
			linesizes_.push_back(linesize);
		};
		const int half_w = (width + 1) / 2;
		const int half_h = (height + 1) / 2;
		switch (format) {
		case core::PixelFormat::kNv12:
			add_plane(width, height);
			add_plane(half_w * 2, half_h);
			break;
		case core::PixelFormat::kI420:
			add_plane(width, height);
			add_plane(half_w, half_h);
			add_plane(half_w, half_h);
			break;
		case core::PixelFormat::kI422:
			add_plane(width, height);
			add_plane(half_w, height);
			add_plane(half_w, height);
			break;
		case core::PixelFormat::kI444:
			add_plane(width, height);
			add_plane(width, height);
			add_plane(width, height);
			break;
		case core::PixelFormat::kYuy2:
		case core::PixelFormat::kYvyu:
		case core::PixelFormat::kUyvy:
			add_plane(half_w * 4, height);
			break;
		default:
			add_plane(width * 4, height);
			break;
		}
		uint32_t state = seed;
		for (auto &plane : planes_) {
			for (auto &byte : plane) {
				state = state * 1664525u + 1013904223u;
				byte = static_cast<uint8_t>(state >> 24);
			}
		}
		for (size_t i = 0; i < planes_.size(); ++i) {
			view_.data[i] = planes_[i].data();
			view_.linesize[i] = linesizes_[i];
		}
	}

	core::FrameView &view() { return view_; }
	std::vector<std::vector<uint8_t>> snapshot() const { return planes_; }

	// 成分の標本を、写し（snapshot）から読む
	uint8_t original(const std::vector<std::vector<uint8_t>> &copy, const core::Component &c, int x, int y) const
	{
		return copy[c.plane][static_cast<size_t>(y) * linesizes_[c.plane] + x * c.step + c.offset];
	}

private:
	core::PixelFormat format_;
	core::FrameView view_;
	std::vector<std::vector<uint8_t>> planes_;
	std::vector<int> linesizes_;
};

} // namespace omask::test
