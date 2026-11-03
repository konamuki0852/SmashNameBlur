#include "infer/ort_engine.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#define ORT_API_MANUAL_INIT
#include <dml_provider_factory.h>
#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>

namespace omask::infer {

namespace {

constexpr const char *kInputName = "image";
constexpr const char *kInputFormat = "rgb_f32_nchw_640x360_0to1";
constexpr const char *kMetadataFormat = "1";
const std::vector<int64_t> kInputShape{1, 3, 360, 640};
constexpr size_t kInputCount = static_cast<size_t>(3) * 360 * 640;

struct ModelSpec {
	const char *kind;
	const char *output_name;
	const char *output_format;
	std::vector<int64_t> output_shape;
};

const ModelSpec kTagSpec{"tagnet", "out", "heat_logit_offset_xy_stride8", {1, 3, 45, 80}};
const ModelSpec kScreenSpec{"screennet", "screen", "screen_logits_4", {1, 4}};

size_t element_count(const std::vector<int64_t> &shape)
{
	size_t n = 1;
	for (int64_t d : shape)
		n *= static_cast<size_t>(d);
	return n;
}

// onnxruntime.dll を、そのフォルダを依存 DLL の検索先にして読み込む（名前だけで読み込まない。他のプラグインとの衝突を避ける）。
// 一度読み込んだら、プロセスが終わるまで外さない
const OrtApi *load_runtime(const std::wstring &dir, uint32_t version, EngineReport &report)
{
	const std::wstring path = dir + L"\\onnxruntime.dll";
	HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
					LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (module == nullptr) {
		report.reason = core::UnavailableReason::kRuntimeMissing;
		report.detail = "onnxruntime.dll could not be loaded (error " + std::to_string(GetLastError()) + ")";
		return nullptr;
	}
	using GetApiBaseFn = const OrtApiBase *(ORT_API_CALL *)();
	const auto get_base = reinterpret_cast<GetApiBaseFn>(GetProcAddress(module, "OrtGetApiBase"));
	if (get_base == nullptr) {
		report.reason = core::UnavailableReason::kRuntimeMissing;
		report.detail = "OrtGetApiBase not found";
		return nullptr;
	}
	const OrtApiBase *base = get_base();
	report.ort_version = base->GetVersionString();
	const OrtApi *api = base->GetApi(version);
	if (api == nullptr) {
		report.reason = core::UnavailableReason::kRuntimeVersion;
		report.detail = "API version " + std::to_string(version) + " is not supported by onnxruntime " +
				report.ort_version;
	}
	return api;
}

bool read_file(const std::wstring &path, std::vector<char> &data)
{
	std::ifstream file(path.c_str(), std::ios::binary);
	if (!file)
		return false;
	data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	return !data.empty();
}

// メタデータと入出力の名前・形を確かめる。合えば版を返す
bool check_model(Ort::Session &session, const ModelSpec &spec, std::string &version, std::string &detail)
{
	Ort::AllocatorWithDefaultOptions allocator;
	Ort::ModelMetadata metadata = session.GetModelMetadata();
	auto get = [&](const char *key) {
		Ort::AllocatedStringPtr value = metadata.LookupCustomMetadataMapAllocated(key, allocator);
		return value ? std::string(value.get()) : std::string();
	};
	if (get("omask.format") != kMetadataFormat || get("omask.model") != spec.kind ||
	    get("omask.input") != kInputFormat || get("omask.output") != spec.output_format) {
		detail = std::string(spec.kind) + ": metadata does not match";
		return false;
	}
	version = get("omask.version");
	if (session.GetInputCount() != 1 || session.GetOutputCount() != 1) {
		detail = std::string(spec.kind) + ": unexpected number of inputs or outputs";
		return false;
	}
	const auto input_name = session.GetInputNameAllocated(0, allocator);
	const auto output_name = session.GetOutputNameAllocated(0, allocator);
	// GetTensorTypeAndShapeInfo は TypeInfo の中を指すだけなので、TypeInfo を変数に持って生かしておく
	const Ort::TypeInfo input_type = session.GetInputTypeInfo(0);
	const Ort::TypeInfo output_type = session.GetOutputTypeInfo(0);
	if (input_type.GetONNXType() != ONNX_TYPE_TENSOR || output_type.GetONNXType() != ONNX_TYPE_TENSOR) {
		detail = std::string(spec.kind) + ": input or output is not a tensor";
		return false;
	}
	const auto input_info = input_type.GetTensorTypeAndShapeInfo();
	const auto output_info = output_type.GetTensorTypeAndShapeInfo();
	const bool ok = std::strcmp(input_name.get(), kInputName) == 0 &&
			std::strcmp(output_name.get(), spec.output_name) == 0 &&
			input_info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
			output_info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
			input_info.GetShape() == kInputShape && output_info.GetShape() == spec.output_shape;
	if (!ok)
		detail = std::string(spec.kind) + ": input or output name/shape does not match";
	return ok;
}

} // namespace

struct OrtEngine::Impl {
	const OrtApi *api = nullptr;
	std::unique_ptr<Ort::Env> env;
	std::vector<char> tag_bytes;
	std::vector<char> screen_bytes;
	std::unique_ptr<Ort::Session> tag_session;
	std::unique_ptr<Ort::Session> screen_session;
	// Ort の値は API の表が無いまま壊すと空の表を引くので、読み込めた後に作るものはすべて unique_ptr で持つ
	std::unique_ptr<Ort::MemoryInfo> memory;
	int cpu_threads = 4;
	int warmup_runs = 3;
	bool gpu = false;
	bool tag_ready = false;
	bool screen_ready = false;
	bool failed = false;
	bool fallback_event = false;
	bool inject_gpu_failure = false;
	std::mutex mutex; // 推論と実行先の切り替えを 1 つずつにする

	Ort::SessionOptions options(bool use_gpu) const
	{
		Ort::SessionOptions so;
		so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
		if (use_gpu) {
			// DirectML の決まり: メモリの並びの最適化を切り、逐次実行にする
			so.DisableMemPattern();
			so.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
			const OrtDmlApi *dml = nullptr;
			Ort::ThrowOnError(api->GetExecutionProviderApi("DML", ORT_API_VERSION,
								       reinterpret_cast<const void **>(&dml)));
			Ort::ThrowOnError(dml->SessionOptionsAppendExecutionProvider_DML(so, 0));
		} else {
			so.SetIntraOpNumThreads(cpu_threads);
			so.SetInterOpNumThreads(1);
		}
		return so;
	}

	bool run(Ort::Session &session, const ModelSpec &spec, const float *input, float *out)
	{
		const char *input_names[] = {kInputName};
		const char *output_names[] = {spec.output_name};
		Ort::Value tensor = Ort::Value::CreateTensor<float>(*memory, const_cast<float *>(input), kInputCount,
								    kInputShape.data(), kInputShape.size());
		auto outputs = session.Run(Ort::RunOptions{nullptr}, input_names, &tensor, 1, output_names, 1);
		const float *data = outputs[0].GetTensorData<float>();
		std::copy(data, data + element_count(spec.output_shape), out);
		return true;
	}

	// 2 つのセッションを作り、確かめ、試しに推論する。実行先の失敗は例外で知らせる（呼ぶ側が CPU に替える）
	void create_sessions(bool use_gpu, EngineReport &report)
	{
		tag_session.reset();
		screen_session.reset();
		const Ort::SessionOptions so = options(use_gpu);
		tag_session = std::make_unique<Ort::Session>(*env, tag_bytes.data(), tag_bytes.size(), so);
		std::string detail;
		tag_ready = check_model(*tag_session, kTagSpec, report.tagnet_version, detail);
		if (!tag_ready) {
			tag_session.reset();
			report.reason = core::UnavailableReason::kModelFormat;
			report.detail = detail;
			return;
		}
		screen_ready = false;
		if (!screen_bytes.empty()) {
			screen_session =
				std::make_unique<Ort::Session>(*env, screen_bytes.data(), screen_bytes.size(), so);
			screen_ready = check_model(*screen_session, kScreenSpec, report.screennet_version, detail);
			if (!screen_ready) {
				screen_session.reset();
				report.detail =
					detail; // screennet だけ使えない: 位置の決まった範囲を常に隠す（PRC-02）
			}
		}
		std::vector<float> zeros(kInputCount, 0.0f);
		std::vector<float> tag_out(element_count(kTagSpec.output_shape));
		std::array<float, 4> screen_out{};
		for (int i = 0; i < warmup_runs; ++i) {
			run(*tag_session, kTagSpec, zeros.data(), tag_out.data());
			if (screen_session)
				run(*screen_session, kScreenSpec, zeros.data(), screen_out.data());
		}
		gpu = use_gpu;
	}

	bool fall_back_to_cpu()
	{
		EngineReport ignored;
		try {
			create_sessions(false, ignored);
		} catch (const std::exception &) {
			tag_ready = false;
			screen_ready = false;
		}
		fallback_event = true;
		failed = !tag_ready;
		return tag_ready;
	}

	template<class Fn> bool guarded(Fn &&fn)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (failed)
			return false;
		try {
			if (inject_gpu_failure && gpu) {
				inject_gpu_failure = false;
				throw std::runtime_error("injected GPU failure");
			}
			return fn();
		} catch (const std::exception &) {
			if (!gpu) {
				failed = true;
				return false;
			}
		}
		// GPU で失敗した: その場で CPU で作り直してやり直す（PRC-08 手順 6）
		if (!fall_back_to_cpu())
			return false;
		try {
			return fn();
		} catch (const std::exception &) {
			failed = true;
			return false;
		}
	}
};

OrtEngine::OrtEngine() : impl_(std::make_unique<Impl>()) {}

OrtEngine::~OrtEngine() = default;

EngineReport OrtEngine::prepare(const EngineOptions &options)
{
	EngineReport report;
	Impl &impl = *impl_;
	std::lock_guard<std::mutex> lock(impl.mutex);
	try {
		impl.api = load_runtime(options.runtime_dir,
					options.api_version != 0 ? options.api_version : ORT_API_VERSION, report);
		if (impl.api == nullptr)
			return report;
		Ort::InitApi(impl.api);
		impl.env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "blur-name");
		impl.env->DisableTelemetryEvents(); // 利用状況を送らない（NFR-07）
		impl.memory = std::make_unique<Ort::MemoryInfo>(
			Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
		impl.cpu_threads = options.cpu_threads;
		impl.warmup_runs = options.warmup_runs;

		if (!read_file(options.tagnet_path, impl.tag_bytes)) {
			report.reason = core::UnavailableReason::kModelMissing;
			report.detail = "tagnet model not found";
			return report;
		}
		if (!read_file(options.screennet_path, impl.screen_bytes))
			impl.screen_bytes.clear();

		// GPU（DirectML）で準備し、実行先の失敗（例外）なら CPU で作り直す。モデルの不一致は実行先によらないので替えない
		bool created = false;
		if (options.prefer_gpu) {
			try {
				impl.create_sessions(true, report);
				created = true;
			} catch (const std::exception &e) {
				report.detail = std::string("DirectML is not available: ") + e.what();
				impl.tag_session.reset();
				impl.screen_session.reset();
				impl.tag_ready = false;
				impl.screen_ready = false;
			}
		}
		if (!created)
			impl.create_sessions(false, report);
		report.ready = impl.tag_ready;
		report.gpu = impl.gpu && impl.tag_ready;
		report.screen_ready = impl.screen_ready;
		if (!impl.tag_ready && report.reason == core::UnavailableReason::kNone)
			report.reason = core::UnavailableReason::kInferenceFailed;
	} catch (const std::exception &e) {
		impl.tag_ready = false;
		impl.screen_ready = false;
		report.ready = false;
		report.reason = core::UnavailableReason::kInferenceFailed;
		report.detail = e.what();
	}
	impl.failed = !impl.tag_ready;
	return report;
}

bool OrtEngine::screen_available() const
{
	return impl_->screen_ready && !impl_->failed;
}

bool OrtEngine::tag_available() const
{
	return impl_->tag_ready && !impl_->failed;
}

bool OrtEngine::run_screen(const float *input, std::array<float, 4> &logits)
{
	Impl &impl = *impl_;
	if (!impl.screen_ready)
		return false;
	return impl.guarded([&] {
		return impl.screen_session && impl.run(*impl.screen_session, kScreenSpec, input, logits.data());
	});
}

bool OrtEngine::run_tags(const float *input, std::vector<float> &output)
{
	Impl &impl = *impl_;
	output.resize(element_count(kTagSpec.output_shape));
	return impl.guarded(
		[&] { return impl.tag_session && impl.run(*impl.tag_session, kTagSpec, input, output.data()); });
}

bool OrtEngine::using_gpu() const
{
	return impl_->gpu;
}

bool OrtEngine::take_fallback_event()
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	const bool event = impl_->fallback_event;
	impl_->fallback_event = false;
	return event;
}

bool OrtEngine::failed() const
{
	return impl_->failed;
}

void OrtEngine::inject_gpu_failure_for_test()
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	impl_->inject_gpu_failure = true;
}

} // namespace omask::infer
