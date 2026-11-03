// 前処理（PRC-04 手順 1・付録 B）。テスト設計書 TC-09
#include "doctest/doctest.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/preprocess.hpp"
#include "frame_builder.hpp"

using omask::core::Component;
using omask::core::component_layout;
using omask::core::kAnalysisHeight;
using omask::core::kAnalysisWidth;
using omask::core::kTensorPlaneSize;
using omask::core::kTensorSize;
using omask::core::PixelFormat;
using omask::core::preprocess_frame;
using omask::core::sample_ptr;
using omask::test::FrameBuilder;

namespace {

// 参照の計算: 3x3（1080p）の平均と、付録 B の式（範囲が限定なら先に丸める）をそのまま書いたもの
std::vector<double> reference(const omask::core::FrameView &f)
{
	std::vector<double> out(kTensorSize);
	const auto layout = component_layout(f.format);
	const bool yuv = f.format != PixelFormat::kBgra;
	for (int oy = 0; oy < kAnalysisHeight; ++oy) {
		for (int ox = 0; ox < kAnalysisWidth; ++ox) {
			const int x0 = ox * f.width / kAnalysisWidth, x1 = (ox + 1) * f.width / kAnalysisWidth;
			const int y0 = oy * f.height / kAnalysisHeight, y1 = (oy + 1) * f.height / kAnalysisHeight;
			double v[3];
			for (int i = 0; i < 3; ++i) {
				const Component &c = layout.components[i];
				double sum = 0;
				int n = 0;
				for (int y = y0 / c.sub_y; y < (y1 + c.sub_y - 1) / c.sub_y; ++y) {
					for (int x = x0 / c.sub_x; x < (x1 + c.sub_x - 1) / c.sub_x; ++x) {
						sum += *sample_ptr(f, c, x, y);
						++n;
					}
				}
				v[i] = sum / n / 255.0;
			}
			double rgb[3];
			if (yuv) {
				if (!f.full_range) {
					for (int i = 0; i < 3; ++i)
						v[i] = std::clamp(v[i], static_cast<double>(f.color_range_min[i]),
								  static_cast<double>(f.color_range_max[i]));
				}
				const auto &m = f.color_matrix;
				for (int r = 0; r < 3; ++r)
					rgb[r] = m[r * 4] * v[0] + m[r * 4 + 1] * v[1] + m[r * 4 + 2] * v[2] +
						 m[r * 4 + 3];
			} else {
				rgb[0] = v[0];
				rgb[1] = v[1];
				rgb[2] = v[2];
			}
			const size_t index = static_cast<size_t>(oy) * kAnalysisWidth + ox;
			for (int c = 0; c < 3; ++c)
				out[c * kTensorPlaneSize + index] = std::clamp(rgb[c], 0.0, 1.0);
		}
	}
	return out;
}

double max_difference(const std::vector<float> &actual, const std::vector<double> &expected)
{
	double worst = 0.0;
	for (size_t i = 0; i < actual.size(); ++i)
		worst = std::max(worst, std::abs(actual[i] - expected[i]));
	return worst;
}

} // namespace

TEST_CASE("TC-09 前処理: 参照の計算との差が 1/255 以内（1080p）")
{
	std::vector<float> tensor(kTensorSize);
	SUBCASE("NV12・限定範囲（範囲の外の値は丸められる）")
	{
		FrameBuilder builder(PixelFormat::kNv12, 1920, 1080, 7);
		const float brightness = preprocess_frame(builder.view(), tensor.data());
		CHECK(max_difference(tensor, reference(builder.view())) <= 1.0 / 255.0);
		double sum = 0.0;
		for (float v : tensor)
			sum += v;
		CHECK(std::abs(brightness - sum / kTensorSize) < 1e-4);
	}
	SUBCASE("I420・全範囲")
	{
		FrameBuilder builder(PixelFormat::kI420, 1920, 1080, 11);
		builder.view().full_range = true;
		preprocess_frame(builder.view(), tensor.data());
		CHECK(max_difference(tensor, reference(builder.view())) <= 1.0 / 255.0);
	}
	SUBCASE("YUY2（詰めた形の 4:2:2）")
	{
		FrameBuilder builder(PixelFormat::kYuy2, 1920, 1080, 13);
		preprocess_frame(builder.view(), tensor.data());
		CHECK(max_difference(tensor, reference(builder.view())) <= 1.0 / 255.0);
	}
	SUBCASE("BGRA（R・G・B をそのまま平均）")
	{
		FrameBuilder builder(PixelFormat::kBgra, 1920, 1080, 17);
		preprocess_frame(builder.view(), tensor.data());
		CHECK(max_difference(tensor, reference(builder.view())) <= 1.0 / 255.0);
	}
}

TEST_CASE("TC-09 前処理: 限定範囲で範囲の下の値は黒、真っ白は 1 になる")
{
	std::vector<float> tensor(kTensorSize);
	FrameBuilder builder(PixelFormat::kI444, 640, 360);
	auto &f = builder.view();
	const auto layout = component_layout(PixelFormat::kI444);
	for (int y = 0; y < 360; ++y) {
		for (int x = 0; x < 640; ++x) {
			*sample_ptr(f, layout.components[0], x, y) = x < 320 ? 5 : 235; // 左は範囲の下、右は白
			*sample_ptr(f, layout.components[1], x, y) = 128;
			*sample_ptr(f, layout.components[2], x, y) = 128;
		}
	}
	preprocess_frame(f, tensor.data());
	CHECK(tensor[0] == doctest::Approx(0.0).epsilon(0.002));                          // R（左上）
	CHECK(tensor[639] == doctest::Approx(1.0).epsilon(0.002));                        // R（右上）
	CHECK(tensor[2 * kTensorPlaneSize + 639] == doctest::Approx(1.0).epsilon(0.002)); // B（右上）
}

TEST_CASE("TC-09 前処理: 対応外の形式は 0 を返す")
{
	std::vector<float> tensor(kTensorSize, 0.5f);
	FrameBuilder builder(PixelFormat::kUnsupported, 640, 360);
	CHECK(preprocess_frame(builder.view(), tensor.data()) == 0.0f);
	CHECK(std::all_of(tensor.begin(), tensor.end(), [](float v) { return v == 0.0f; }));
}

TEST_CASE("TC-19 学習の前処理（ml/omask_ml/preprocess.py）と本番の前処理が同じ値を出す")
{
	// ml/omask_ml/fixtures.py の lcg_nv12 と同じ式で 1920x1080 の NV12 を作る
	const int width = 1920;
	const int height = 1080;
	std::vector<uint8_t> nv12(static_cast<size_t>(width) * height * 3 / 2);
	uint32_t state = 4321;
	for (uint8_t &byte : nv12) {
		state = state * 1664525u + 1013904223u;
		byte = static_cast<uint8_t>(state >> 24);
	}
	omask::core::FrameView frame;
	frame.format = omask::core::PixelFormat::kNv12;
	frame.width = width;
	frame.height = height;
	frame.data[0] = nv12.data();
	frame.data[1] = nv12.data() + static_cast<size_t>(width) * height;
	frame.linesize[0] = width;
	frame.linesize[1] = width;
	frame.color_matrix = omask::core::kBt709LimitedMatrix;
	frame.color_range_min = omask::core::kBt709LimitedMin;
	frame.color_range_max = omask::core::kBt709LimitedMax;
	std::vector<float> out(omask::core::kTensorSize);
	const float brightness = omask::core::preprocess_frame(frame, out.data());

	std::ifstream file(OMASK_TEST_DATA_DIR "/preprocess_ref.bin", std::ios::binary);
	REQUIRE(file);
	std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	std::vector<float> ref(bytes.size() / sizeof(float));
	std::memcpy(ref.data(), bytes.data(), ref.size() * sizeof(float));
	const size_t stride = 97; // fixtures.py の PREPROCESS_STRIDE
	REQUIRE(ref.size() == (out.size() + stride - 1) / stride + 1);
	double worst = 0.0;
	for (size_t i = 0; i + 1 < ref.size(); ++i)
		worst = std::max(worst, static_cast<double>(std::abs(out[i * stride] - ref[i])));
	CHECK(worst <= 1e-4);
	CHECK(brightness == doctest::Approx(ref.back()).epsilon(1e-4));
}
