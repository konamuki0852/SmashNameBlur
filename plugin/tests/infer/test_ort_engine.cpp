// 推論エンジン（PRC-08）の結合テスト。実物の onnxruntime.dll（テストの実行ファイルと同じフォルダに置く）と
// 同梱のモデル（data/models）、参照（tests/data。ml/omask_ml/fixtures.py で作る）を使う。テスト設計書 TC-16〜18
#include "doctest/doctest.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/preprocess.hpp"
#include "infer/ort_engine.hpp"

#define OMASK_WIDEN2(x) L##x
#define OMASK_WIDEN(x) OMASK_WIDEN2(x)

using omask::core::UnavailableReason;
using omask::infer::EngineOptions;
using omask::infer::EngineReport;
using omask::infer::OrtEngine;

namespace {

const std::wstring kModels = OMASK_WIDEN(OMASK_MODELS_DIR);
const std::wstring kData = OMASK_WIDEN(OMASK_TEST_DATA_DIR);

std::wstring exe_dir()
{
	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	return std::filesystem::path(path).parent_path().wstring();
}

EngineOptions cpu_options()
{
	EngineOptions o;
	o.runtime_dir = exe_dir();
	o.tagnet_path = kModels + L"/tagnet.onnx";
	o.screennet_path = kModels + L"/screennet.onnx";
	o.prefer_gpu = false;
	o.warmup_runs = 1;
	return o;
}

// ml/omask_ml/fixtures.py の reference_input と同じ式
std::vector<float> reference_input()
{
	std::vector<float> values(omask::core::kTensorSize);
	uint32_t state = 12345;
	for (float &v : values) {
		state = state * 1664525u + 1013904223u;
		v = static_cast<float>(state >> 8) / 16777216.0f;
	}
	return values;
}

std::vector<float> read_floats(const std::wstring &path)
{
	std::ifstream file(path.c_str(), std::ios::binary);
	std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	std::vector<float> values(bytes.size() / sizeof(float));
	std::memcpy(values.data(), bytes.data(), values.size() * sizeof(float));
	return values;
}

double max_diff(const float *a, const std::vector<float> &b, size_t n)
{
	double worst = 0.0;
	for (size_t i = 0; i < n; ++i)
		worst = std::max(worst, static_cast<double>(std::abs(a[i] - b[i])));
	return worst;
}

} // namespace

TEST_CASE("TC-16 (a) CPU で準備できる（2 つのセッション、版がわかる）")
{
	OrtEngine engine;
	const EngineReport r = engine.prepare(cpu_options());
	CHECK(r.ready);
	CHECK(r.screen_ready);
	CHECK_FALSE(r.gpu);
	CHECK(r.ort_version == "1.24.4");
	CHECK(r.tagnet_version == "v1");
	CHECK(r.screennet_version == "v1");
	CHECK(engine.tag_available());
	CHECK(engine.screen_available());
}

TEST_CASE("TC-16 (b) onnxruntime.dll が無ければ「検出できない」（推論ライブラリが見つからない）")
{
	const auto empty = std::filesystem::temp_directory_path() / "omask_empty_runtime";
	std::filesystem::create_directories(empty);
	EngineOptions o = cpu_options();
	o.runtime_dir = empty.wstring();
	OrtEngine engine;
	const EngineReport r = engine.prepare(o);
	CHECK_FALSE(r.ready);
	CHECK(r.reason == UnavailableReason::kRuntimeMissing);
	CHECK_FALSE(engine.tag_available());
}

TEST_CASE("TC-16 (c) 版の合わない推論ライブラリは使わない（推論ライブラリの版が合わない）")
{
	EngineOptions o = cpu_options();
	o.api_version = 999;
	OrtEngine engine;
	const EngineReport r = engine.prepare(o);
	CHECK_FALSE(r.ready);
	CHECK(r.reason == UnavailableReason::kRuntimeVersion);
}

TEST_CASE("TC-16 (d)(e) メタデータ・入出力の形の違うモデルは使わない")
{
	for (const wchar_t *bad : {L"/bad_meta.onnx", L"/bad_shape.onnx"}) {
		{ // tagnet の場所に置くと「検出できない」（モデルの形式が合わない）
			EngineOptions o = cpu_options();
			o.tagnet_path = kData + bad;
			OrtEngine engine;
			const EngineReport r = engine.prepare(o);
			CHECK_FALSE(r.ready);
			CHECK(r.reason == UnavailableReason::kModelFormat);
		}
		{ // screennet の場所に置くと、screennet だけ使わない（頭上タグは動く）
			EngineOptions o = cpu_options();
			o.screennet_path = kData + bad;
			OrtEngine engine;
			const EngineReport r = engine.prepare(o);
			CHECK(r.ready);
			CHECK_FALSE(r.screen_ready);
			CHECK_FALSE(engine.screen_available());
		}
	}
}

TEST_CASE("TC-16 モデルのファイルが無ければ「検出できない」（モデルが見つからない）")
{
	EngineOptions o = cpu_options();
	o.tagnet_path = kModels + L"/missing.onnx";
	OrtEngine engine;
	CHECK(engine.prepare(o).reason == UnavailableReason::kModelMissing);
}

TEST_CASE("TC-17 C++（CPU）と Python の onnxruntime の出力の差が 1e-4 以内")
{
	OrtEngine engine;
	REQUIRE(engine.prepare(cpu_options()).ready);
	const std::vector<float> input = reference_input();
	std::vector<float> tags;
	REQUIRE(engine.run_tags(input.data(), tags));
	const std::vector<float> tag_ref = read_floats(kData + L"/tagnet_ref.bin");
	REQUIRE(tags.size() == tag_ref.size());
	CHECK(max_diff(tags.data(), tag_ref, tags.size()) <= 1e-4);

	std::array<float, 4> logits{};
	REQUIRE(engine.run_screen(input.data(), logits));
	const std::vector<float> screen_ref = read_floats(kData + L"/screennet_ref.bin");
	REQUIRE(screen_ref.size() == 4);
	CHECK(max_diff(logits.data(), screen_ref, 4) <= 1e-4);
}

TEST_CASE("TC-18 GPU の推論に失敗したら CPU で作り直して続ける")
{
	EngineOptions o = cpu_options();
	o.prefer_gpu = true;
	OrtEngine engine;
	const EngineReport r = engine.prepare(o);
	INFO("detail: " << r.detail);
	REQUIRE(r.ready);
	if (!r.gpu) {
		MESSAGE("DirectML が使えない環境なので、GPU からの切り替えは確かめられない: " << r.detail);
		return;
	}
	const std::vector<float> input = reference_input();
	const std::vector<float> tag_ref = read_floats(kData + L"/tagnet_ref.bin");
	std::vector<float> gpu_tags;
	REQUIRE(engine.run_tags(input.data(), gpu_tags));
	CHECK(max_diff(gpu_tags.data(), tag_ref, gpu_tags.size()) <= 1e-2); // GPU は計算の順が違うので少し広く
	CHECK_FALSE(engine.take_fallback_event());

	engine.inject_gpu_failure_for_test();
	std::vector<float> tags;
	REQUIRE(engine.run_tags(input.data(), tags));
	CHECK_FALSE(engine.using_gpu());
	CHECK(engine.take_fallback_event());
	CHECK_FALSE(engine.take_fallback_event()); // 1 回だけ
	CHECK(max_diff(tags.data(), tag_ref, tags.size()) <= 1e-4);
	CHECK(engine.screen_available());
}
