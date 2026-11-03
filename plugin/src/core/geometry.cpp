#include "core/geometry.hpp"

#include <algorithm>
#include <cmath>

namespace omask::core {

namespace {

int floor_to_int(double value)
{
	return static_cast<int>(std::floor(value));
}

int ceil_to_int(double value)
{
	return static_cast<int>(std::ceil(value));
}

// 整数の割り算の切り下げ・切り上げ（負の値でも正しく丸める）。比例の換算を浮動小数点の誤差なしに行う
int64_t floor_div(int64_t num, int64_t den)
{
	const int64_t q = num / den;
	return (num % den != 0 && ((num < 0) != (den < 0))) ? q - 1 : q;
}

int64_t ceil_div(int64_t num, int64_t den)
{
	return -floor_div(-num, den);
}

} // namespace

RectI clip_rect(const RectI &rect, int width, int height)
{
	RectI out;
	out.x0 = std::clamp(rect.x0, 0, width);
	out.y0 = std::clamp(rect.y0, 0, height);
	out.x1 = std::clamp(rect.x1, 0, width);
	out.y1 = std::clamp(rect.y1, 0, height);
	if (out.empty())
		return RectI{};
	return out;
}

RectI scale_reference_rect(const ReferenceRect &rect, int margin, int width, int height)
{
	RectI out;
	out.x0 = static_cast<int>(floor_div(static_cast<int64_t>(rect.x0 - margin) * width, kReferenceWidth));
	out.y0 = static_cast<int>(floor_div(static_cast<int64_t>(rect.y0 - margin) * height, kReferenceHeight));
	out.x1 = static_cast<int>(ceil_div(static_cast<int64_t>(rect.x1 + margin) * width, kReferenceWidth));
	out.y1 = static_cast<int>(ceil_div(static_cast<int64_t>(rect.y1 + margin) * height, kReferenceHeight));
	return clip_rect(out, width, height);
}

RectI scale_analysis_box(PointF center, float half_width, float half_height, int width, int height)
{
	const double sx = static_cast<double>(width) / kAnalysisWidth;
	const double sy = static_cast<double>(height) / kAnalysisHeight;
	RectI out;
	out.x0 = floor_to_int((center.x - half_width) * sx);
	out.y0 = floor_to_int((center.y - half_height) * sy);
	out.x1 = ceil_to_int((center.x + half_width) * sx);
	out.y1 = ceil_to_int((center.y + half_height) * sy);
	return clip_rect(out, width, height);
}

} // namespace omask::core
