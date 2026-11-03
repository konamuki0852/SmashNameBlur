#include "core/preprocess.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <emmintrin.h>

#include "core/color.hpp"

namespace omask::core {

namespace {

// 出力の i 番目が受け持つ元の範囲 [begin, end)。i*n/m 〜 (i+1)*n/m、少なくとも 1 画素
struct Span {
	int begin;
	int end;
};

std::vector<Span> spans(int source, int target)
{
	std::vector<Span> out(static_cast<size_t>(target));
	for (int i = 0; i < target; ++i) {
		int begin = static_cast<int>(static_cast<int64_t>(i) * source / target);
		int end = static_cast<int>(static_cast<int64_t>(i + 1) * source / target);
		begin = std::min(begin, source - 1);
		end = std::max(end, begin + 1);
		out[static_cast<size_t>(i)] = Span{begin, end};
	}
	return out;
}

// 間引きのある成分では、受け持つ画素の範囲にかかる標本を外側へ丸めて取る
std::vector<Span> component_spans(const std::vector<Span> &pixel_spans, int sub, int limit)
{
	std::vector<Span> out(pixel_spans.size());
	for (size_t i = 0; i < pixel_spans.size(); ++i) {
		const int begin = pixel_spans[i].begin / sub;
		const int end = std::min((pixel_spans[i].end + sub - 1) / sub, limit);
		out[i] = Span{begin, std::max(end, begin + 1)};
	}
	return out;
}

// 平面ごとの、受け持ちの行の縦の合計（バイトのまま）。同じ平面の成分（NV12 の U・V、YUY2 の Y・U・V など）で共有する
struct PlaneSums {
	int plane = -1;
	int row_bytes = 0;
	std::vector<Span> rows;
	std::vector<uint16_t> sums;

	void accumulate(const FrameView &frame, int oy)
	{
		std::fill(sums.begin(), sums.end(), static_cast<uint16_t>(0));
		const Span r = rows[static_cast<size_t>(oy)];
		uint16_t *s = sums.data();
		const __m128i zero = _mm_setzero_si128();
		for (int y = r.begin; y < r.end; ++y) {
			const uint8_t *p = frame.data[plane] + static_cast<int64_t>(y) * frame.linesize[plane];
			int b = 0;
			// 16 バイトずつ 16 bit に広げて足す（x64 は SSE2 を必ず持つ）
			for (; b + 16 <= row_bytes; b += 16) {
				const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(p + b));
				__m128i *d = reinterpret_cast<__m128i *>(s + b);
				_mm_storeu_si128(d, _mm_add_epi16(_mm_loadu_si128(d), _mm_unpacklo_epi8(v, zero)));
				_mm_storeu_si128(d + 1,
						 _mm_add_epi16(_mm_loadu_si128(d + 1), _mm_unpackhi_epi8(v, zero)));
			}
			for (; b < row_bytes; ++b)
				s[b] = static_cast<uint16_t>(s[b] + p[b]);
		}
	}
};

// 成分の受け持ちの列から平均を作る
struct ComponentPick {
	int plane_index = 0; // PlaneSums の番号
	int step = 1;
	int offset = 0;
	int uniform_length = 0; // 受け持ちの長さがすべて同じならその長さ（0 はそろっていない）
	std::vector<Span> cols;
	std::vector<int> start;   // 受け持ちの先頭（縦の合計の配列の位置）
	std::vector<float> scale; // 1 / (列の数 × 255)
};

// 受け持ちの長さ L が決まっている場合（1080p では輝度 3・色差 2）。内側のループを展開できる
template<int L> void pick_row(const uint16_t *s, const ComponentPick &pick, float row_scale, float *means)
{
	const int step = pick.step;
	for (int ox = 0; ox < kAnalysisWidth; ++ox) {
		const uint16_t *q = s + pick.start[static_cast<size_t>(ox)];
		uint32_t sum = 0;
		for (int k = 0; k < L; ++k)
			sum += q[k * step];
		means[ox] = static_cast<float>(sum) * pick.scale[static_cast<size_t>(ox)] * row_scale;
	}
}

void pick_row_generic(const uint16_t *s, const ComponentPick &pick, float row_scale, float *means)
{
	for (int ox = 0; ox < kAnalysisWidth; ++ox) {
		const Span cols = pick.cols[static_cast<size_t>(ox)];
		uint32_t sum = 0;
		for (int x = cols.begin; x < cols.end; ++x)
			sum += s[static_cast<int64_t>(x) * pick.step];
		means[ox] = static_cast<float>(sum) * pick.scale[static_cast<size_t>(ox)] * row_scale;
	}
}

} // namespace

float preprocess_frame(const FrameView &frame, float *out)
{
	std::fill(out, out + kTensorSize, 0.0f);
	if (!is_supported(frame.format) || frame.width <= 0 || frame.height <= 0)
		return 0.0f;

	const ComponentLayout layout = component_layout(frame.format);
	const bool yuv = is_yuv(frame.format);
	const std::vector<Span> xs = spans(frame.width, kAnalysisWidth);
	const std::vector<Span> ys = spans(frame.height, kAnalysisHeight);

	std::vector<PlaneSums> planes;
	std::array<ComponentPick, 3> picks;
	for (int i = 0; i < 3; ++i) {
		const Component &c = layout.components[i];
		const int width = component_width(frame, c);
		auto it = std::find_if(planes.begin(), planes.end(),
				       [&](const PlaneSums &p) { return p.plane == c.plane; });
		if (it == planes.end()) {
			PlaneSums p;
			p.plane = c.plane;
			p.rows = component_spans(ys, c.sub_y, component_height(frame, c));
			planes.push_back(std::move(p));
			it = planes.end() - 1;
		}
		it->row_bytes = std::max(it->row_bytes, (width - 1) * c.step + c.offset + 1);
		ComponentPick &pick = picks[static_cast<size_t>(i)];
		pick.plane_index = static_cast<int>(it - planes.begin());
		pick.step = c.step;
		pick.offset = c.offset;
		pick.cols = component_spans(xs, c.sub_x, width);
	}
	for (PlaneSums &p : planes)
		p.sums.assign(static_cast<size_t>(p.row_bytes), 0);
	for (ComponentPick &pick : picks) {
		pick.scale.resize(pick.cols.size());
		pick.start.resize(pick.cols.size());
		pick.uniform_length = pick.cols.front().end - pick.cols.front().begin;
		for (size_t k = 0; k < pick.cols.size(); ++k) {
			const int length = pick.cols[k].end - pick.cols[k].begin;
			pick.scale[k] = 1.0f / (static_cast<float>(length) * 255.0f);
			pick.start[k] = pick.cols[k].begin * pick.step;
			if (length != pick.uniform_length)
				pick.uniform_length = 0;
		}
	}

	// 色の変換（付録 B）。行列と範囲は 1 コマに 1 回だけ用意する
	std::array<float, 16> m = frame.color_matrix;
	if (std::all_of(m.begin(), m.end(), [](float v) { return v == 0.0f; }))
		m = kBt709LimitedMatrix;
	std::array<float, 3> lo{0.0f, 0.0f, 0.0f};
	std::array<float, 3> hi{1.0f, 1.0f, 1.0f};
	if (yuv && !frame.full_range) {
		lo = frame.color_range_min;
		hi = frame.color_range_max;
	}

	std::array<std::array<float, kAnalysisWidth>, 3> means{};
	float *r_plane = out;
	float *g_plane = out + kTensorPlaneSize;
	float *b_plane = out + 2 * kTensorPlaneSize;
	double total = 0.0;
	for (int oy = 0; oy < kAnalysisHeight; ++oy) {
		for (PlaneSums &p : planes)
			p.accumulate(frame, oy);
		for (size_t i = 0; i < 3; ++i) {
			const ComponentPick &pick = picks[i];
			const PlaneSums &p = planes[static_cast<size_t>(pick.plane_index)];
			const Span r = p.rows[static_cast<size_t>(oy)];
			const float row_scale = 1.0f / static_cast<float>(r.end - r.begin);
			const uint16_t *s = p.sums.data() + pick.offset;
			float *row_means = means[i].data();
			switch (pick.uniform_length) {
			case 1:
				pick_row<1>(s, pick, row_scale, row_means);
				break;
			case 2:
				pick_row<2>(s, pick, row_scale, row_means);
				break;
			case 3:
				pick_row<3>(s, pick, row_scale, row_means);
				break;
			case 4:
				pick_row<4>(s, pick, row_scale, row_means);
				break;
			default:
				pick_row_generic(s, pick, row_scale, row_means);
				break;
			}
		}
		const size_t base = static_cast<size_t>(oy) * kAnalysisWidth;
		float row_total = 0.0f;
		for (int ox = 0; ox < kAnalysisWidth; ++ox) {
			float a = means[0][static_cast<size_t>(ox)];
			float b = means[1][static_cast<size_t>(ox)];
			float c = means[2][static_cast<size_t>(ox)];
			float r = a;
			float g = b;
			float bl = c;
			if (yuv) {
				a = std::clamp(a, lo[0], hi[0]);
				b = std::clamp(b, lo[1], hi[1]);
				c = std::clamp(c, lo[2], hi[2]);
				r = m[0] * a + m[1] * b + m[2] * c + m[3];
				g = m[4] * a + m[5] * b + m[6] * c + m[7];
				bl = m[8] * a + m[9] * b + m[10] * c + m[11];
			}
			r = std::clamp(r, 0.0f, 1.0f);
			g = std::clamp(g, 0.0f, 1.0f);
			bl = std::clamp(bl, 0.0f, 1.0f);
			r_plane[base + static_cast<size_t>(ox)] = r;
			g_plane[base + static_cast<size_t>(ox)] = g;
			b_plane[base + static_cast<size_t>(ox)] = bl;
			row_total += r + g + bl;
		}
		total += row_total;
	}
	return static_cast<float>(total / static_cast<double>(kTensorSize));
}

} // namespace omask::core
