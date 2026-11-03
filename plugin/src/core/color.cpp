#include "core/color.hpp"

#include <algorithm>
#include <cmath>

namespace omask::core {

namespace {

// 行列が入っていない（すべて 0）ときは BT.709 の限定範囲とみなす
const std::array<float, 16> &matrix_of(const FrameView &frame)
{
	const bool empty = std::all_of(frame.color_matrix.begin(), frame.color_matrix.end(),
				       [](float value) { return value == 0.0f; });
	return empty ? kBt709LimitedMatrix : frame.color_matrix;
}

uint8_t to_byte(double value)
{
	return static_cast<uint8_t>(std::clamp(std::lround(value * 255.0), 0L, 255L));
}

} // namespace

std::array<float, 3> yuv_to_rgb(const FrameView &frame, float y, float u, float v)
{
	if (!frame.full_range) {
		y = std::clamp(y, frame.color_range_min[0], frame.color_range_max[0]);
		u = std::clamp(u, frame.color_range_min[1], frame.color_range_max[1]);
		v = std::clamp(v, frame.color_range_min[2], frame.color_range_max[2]);
	}
	const auto &m = matrix_of(frame);
	return {m[0] * y + m[1] * u + m[2] * v + m[3], m[4] * y + m[5] * u + m[6] * v + m[7],
		m[8] * y + m[9] * u + m[10] * v + m[11]};
}

std::array<uint8_t, 3> rgb_to_yuv_bytes(const FrameView &frame, float r, float g, float b)
{
	const auto &m = matrix_of(frame);
	// [r g b] = A [y u v] + t を解く（A は行列の左上 3x3、t は 4 列目）
	const double a00 = m[0], a01 = m[1], a02 = m[2];
	const double a10 = m[4], a11 = m[5], a12 = m[6];
	const double a20 = m[8], a21 = m[9], a22 = m[10];
	const double c0 = r - m[3], c1 = g - m[7], c2 = b - m[11];
	const double det =
		a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
	if (std::abs(det) < 1e-9)
		return {0, 128, 128};
	const double y =
		(c0 * (a11 * a22 - a12 * a21) - a01 * (c1 * a22 - a12 * c2) + a02 * (c1 * a21 - a11 * c2)) / det;
	const double u =
		(a00 * (c1 * a22 - a12 * c2) - c0 * (a10 * a22 - a12 * a20) + a02 * (a10 * c2 - c1 * a20)) / det;
	const double v =
		(a00 * (a11 * c2 - c1 * a21) - a01 * (a10 * c2 - c1 * a20) + c0 * (a10 * a21 - a11 * a20)) / det;
	const auto clamp_component = [&](double value, int index) {
		const double lo = frame.full_range ? 0.0 : frame.color_range_min[index];
		const double hi = frame.full_range ? 1.0 : frame.color_range_max[index];
		return std::clamp(value, lo, hi);
	};
	return {to_byte(clamp_component(y, 0)), to_byte(clamp_component(u, 1)), to_byte(clamp_component(v, 2))};
}

} // namespace omask::core
