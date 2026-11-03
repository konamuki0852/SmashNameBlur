#include "core/result_band.hpp"

#include <algorithm>

#include "core/geometry.hpp"
#include "core/params.hpp"
#include "core/preprocess.hpp"

namespace omask::core {

namespace {

// 1080p 換算の横位置を解析座標の列に直す（左は切り捨て、右は切り上げ）
int to_column(int reference_x, bool round_up)
{
	const int scaled = reference_x * kAnalysisWidth;
	const int column = round_up ? (scaled + kReferenceWidth - 1) / kReferenceWidth : scaled / kReferenceWidth;
	return std::clamp(column, 0, kAnalysisWidth);
}

} // namespace

std::vector<ResultBand> find_result_bands(const float *tensor, const ResultBandParams &params)
{
	const int x0 = to_column(params.column_x0, false);
	const int x1 = to_column(params.column_x1, true);
	const int width = x1 - x0;
	std::vector<ResultBand> bands;
	if (width <= 0)
		return bands;
	const float *red = tensor;
	const float *green = tensor + kTensorPlaneSize;
	const float *blue = tensor + 2 * kTensorPlaneSize;
	const int ignore_row = params.ignore_below * kAnalysisHeight / kReferenceHeight;
	int run_start = -1;
	for (int y = 0; y <= kAnalysisHeight; ++y) {
		bool dark_row = false;
		if (y < kAnalysisHeight) {
			int dark = 0;
			const std::size_t row = static_cast<std::size_t>(y) * kAnalysisWidth;
			for (int x = x0; x < x1; ++x) {
				const std::size_t i = row + static_cast<std::size_t>(x);
				if (red[i] < params.dark_max && green[i] < params.dark_max && blue[i] < params.dark_max)
					++dark;
			}
			dark_row = static_cast<float>(dark) >= params.dark_fraction * static_cast<float>(width);
		}
		if (dark_row && run_start < 0) {
			run_start = y;
		} else if (!dark_row && run_start >= 0) {
			if (y - run_start >= params.min_rows && run_start < ignore_row)
				bands.push_back(ResultBand{run_start, y - run_start});
			run_start = -1;
		}
	}
	return bands;
}

bool is_result_detail(const std::vector<ResultBand> &bands, const ResultBandParams &params)
{
	return std::any_of(bands.begin(), bands.end(), [&](const ResultBand &band) {
		const int top = band.top * kReferenceHeight / kAnalysisHeight;
		return top >= params.detail_top_min && top <= params.detail_top_max &&
		       band.rows >= params.detail_min_rows;
	});
}

std::vector<MaskRegion> result_band_regions(const ResultStore &store, int64_t output_ns, const ResultBandParams &params,
					    int width, int height)
{
	std::vector<MaskRegion> regions;
	const int64_t hold = params.hold_ms * kNsPerMs;
	const int64_t look = params.look_ahead_ms * kNsPerMs;
	store.for_each_in(output_ns - hold, output_ns + look, [&](const ResultStore::Entry &entry) {
		if (!entry.done || entry.result.status != AnalysisStatus::kOk ||
		    is_result_detail(entry.result.result_bands, params))
			return;
		for (const ResultBand &band : entry.result.result_bands) {
			const int top = band.top * kReferenceHeight / kAnalysisHeight;
			const ReferenceRect rect{params.column_x0, top - params.above, params.column_x1,
						 top + params.below};
			const RectI scaled = scale_reference_rect(rect, params.margin, width, height);
			if (scaled.empty())
				continue;
			const bool duplicate =
				std::any_of(regions.begin(), regions.end(),
					    [&](const MaskRegion &region) { return region.rect == scaled; });
			if (!duplicate)
				regions.push_back(MaskRegion{scaled, RegionKind::kFixed});
		}
	});
	return regions;
}

} // namespace omask::core
