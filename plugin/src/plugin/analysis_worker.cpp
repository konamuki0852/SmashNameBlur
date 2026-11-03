#include "plugin/analysis_worker.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

#include "core/analyzer.hpp"

namespace omask::plugin {

namespace {

enum class SlotState { kFree, kFilling, kQueued, kWorking };

struct Slot {
	std::array<std::vector<uint8_t>, 4> planes;
	core::FrameView view;
	int64_t timestamp_ns = 0;
	uint64_t generation = 0;
	SlotState state = SlotState::kFree;
};

double elapsed_ms(std::chrono::steady_clock::time_point since)
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

} // namespace

// 描画スレッドと解析スレッドで共有する。解析スレッドを切り離しても生き残るよう shared_ptr で持つ
struct AnalysisWorker::State {
	infer::EngineOptions options;

	std::mutex mutex; // 待ち行列・バッファの状態・集計・止める合図
	std::condition_variable wake;
	std::condition_variable finished_cv;
	std::array<Slot, kPoolSize> slots;
	std::deque<int> queue;
	core::TimingWindow timing;
	core::TimingWindow preprocess_timing;
	core::TimingWindow inference_timing;
	bool stop = false;
	bool finished = false;

	std::mutex listener_mutex; // 知らせの最中に止められないようにする
	AnalysisListener *listener = nullptr;

	std::atomic<bool> ready{false};
	std::atomic<bool> tag_ok{false};
	std::atomic<bool> screen_ok{false};
};

AnalysisWorker::AnalysisWorker(infer::EngineOptions options, AnalysisListener *listener)
	: state_(std::make_shared<State>())
{
	state_->options = std::move(options);
	state_->listener = listener;
}

AnalysisWorker::~AnalysisWorker()
{
	if (thread_.joinable())
		stop(std::chrono::milliseconds(2000));
}

bool AnalysisWorker::start()
{
	try {
		thread_ = std::thread(&AnalysisWorker::run, state_);
		return true;
	} catch (const std::exception &) {
		return false;
	}
}

bool AnalysisWorker::ready() const
{
	return state_->ready.load();
}

bool AnalysisWorker::tag_available() const
{
	return state_->ready.load() && state_->tag_ok.load();
}

bool AnalysisWorker::screen_available() const
{
	return state_->ready.load() && state_->screen_ok.load();
}

bool AnalysisWorker::submit(const core::FrameView &frame, bool flip, int64_t timestamp_ns, uint64_t generation)
{
	State &s = *state_;
	if (!s.ready.load() || !s.tag_ok.load())
		return false;
	int index = -1;
	{
		std::lock_guard<std::mutex> lock(s.mutex);
		if (s.stop)
			return false;
		for (int i = 0; i < kPoolSize; ++i) {
			if (s.slots[static_cast<size_t>(i)].state == SlotState::kFree) {
				index = i;
				break;
			}
		}
		if (index < 0)
			return false;
		s.slots[static_cast<size_t>(index)].state = SlotState::kFilling;
	}

	// 写すのはロックの外で（解析スレッドを待たせない）。平面ごとに 1 行のバイト数ごと丸ごと写す
	Slot &slot = s.slots[static_cast<size_t>(index)];
	slot.view = frame;
	const int planes = core::plane_count(frame.format);
	for (int p = 0; p < planes; ++p) {
		const size_t i = static_cast<size_t>(p);
		const size_t bytes =
			static_cast<size_t>(frame.linesize[i]) * static_cast<size_t>(core::plane_rows(frame, p));
		if (slot.planes[i].size() != bytes)
			slot.planes[i].resize(bytes);
		std::memcpy(slot.planes[i].data(), frame.data[i], bytes);
		slot.view.data[i] = slot.planes[i].data();
	}
	if (flip)
		core::flip_vertically(slot.view);

	{
		std::lock_guard<std::mutex> lock(s.mutex);
		slot.timestamp_ns = timestamp_ns;
		slot.generation = generation;
		slot.state = SlotState::kQueued;
		s.queue.push_back(index);
	}
	s.wake.notify_one();
	return true;
}

AnalysisTiming AnalysisWorker::take_timing()
{
	std::lock_guard<std::mutex> lock(state_->mutex);
	return AnalysisTiming{state_->timing.take(), state_->preprocess_timing.take(), state_->inference_timing.take()};
}

bool AnalysisWorker::stop(std::chrono::milliseconds timeout)
{
	State &s = *state_;
	{
		std::lock_guard<std::mutex> lock(s.listener_mutex);
		s.listener = nullptr;
	}
	{
		std::lock_guard<std::mutex> lock(s.mutex);
		s.stop = true;
	}
	s.wake.notify_all();
	if (!thread_.joinable())
		return true;
	bool done = false;
	{
		std::unique_lock<std::mutex> lock(s.mutex);
		done = s.finished_cv.wait_for(lock, timeout, [&] { return s.finished; });
	}
	if (done)
		thread_.join();
	else
		thread_.detach(); // 終わり次第、共有の状態とエンジンは解析スレッドが片付ける
	return done;
}

void AnalysisWorker::run(std::shared_ptr<State> state)
{
	State &s = *state;
	try {
		infer::OrtEngine engine;
		const auto prepare_start = std::chrono::steady_clock::now();
		const infer::EngineReport report = engine.prepare(s.options);
		const double prepare_ms = elapsed_ms(prepare_start);
		s.tag_ok = engine.tag_available();
		s.screen_ok = engine.screen_available();
		{
			std::lock_guard<std::mutex> lock(s.listener_mutex);
			if (s.listener != nullptr)
				s.listener->on_engine_report(report, prepare_ms);
		}
		s.ready = report.ready;

		core::Analyzer analyzer;
		uint64_t generation = 0;
		bool first = true;
		bool failure_reported = false;
		for (;;) {
			int index = -1;
			{
				std::unique_lock<std::mutex> lock(s.mutex);
				s.wake.wait(lock, [&] { return s.stop || !s.queue.empty(); });
				if (s.stop)
					break;
				index = s.queue.front();
				s.queue.pop_front();
				s.slots[static_cast<size_t>(index)].state = SlotState::kWorking;
			}
			Slot &slot = s.slots[static_cast<size_t>(index)];
			// 時刻の飛びでやり直した（世代が変わった）ら、画面判定の記憶を捨てる
			if (first || slot.generation != generation) {
				analyzer.reset();
				generation = slot.generation;
				first = false;
			}
			const auto start = std::chrono::steady_clock::now();
			const core::AnalysisResult result = analyzer.analyze(slot.view, slot.timestamp_ns, &engine);
			const double ms = elapsed_ms(start);
			const bool fallback = engine.take_fallback_event();
			const bool failed = engine.failed();
			s.tag_ok = engine.tag_available();
			s.screen_ok = engine.screen_available();
			const uint64_t slot_generation = slot.generation;
			{
				std::lock_guard<std::mutex> lock(s.mutex);
				slot.state = SlotState::kFree;
				s.timing.add(ms);
				const core::AnalyzerTiming &parts = analyzer.last_timing();
				s.preprocess_timing.add(parts.preprocess_ms);
				s.inference_timing.add(parts.screen_ms + parts.tags_ms);
			}
			std::lock_guard<std::mutex> lock(s.listener_mutex);
			if (s.listener == nullptr)
				continue;
			if (fallback)
				s.listener->on_gpu_fallback();
			if (failed && !failure_reported) {
				failure_reported = true;
				s.listener->on_engine_failed();
			}
			s.listener->on_result(slot_generation, result);
		}
	} catch (...) {
		// 解析スレッドの外に例外を出さない。以後は推論しない（位置の決まった範囲は描画スレッドで隠し続ける）
		s.tag_ok = false;
		s.screen_ok = false;
		std::lock_guard<std::mutex> lock(s.listener_mutex);
		if (s.listener != nullptr)
			s.listener->on_engine_failed();
	}
	{
		std::lock_guard<std::mutex> lock(s.mutex);
		s.finished = true;
	}
	s.finished_cv.notify_all();
}

} // namespace omask::plugin
