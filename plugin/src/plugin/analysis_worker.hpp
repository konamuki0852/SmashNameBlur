// 解析スレッド（処理詳細設計書 PRC-01 手順 5・PRC-04・PRC-08）。描画スレッドから写したコマを受け取り、
// 推論エンジンを準備して、前処理 → 画面判定 → 頭上タグの推論を行い、結果を知らせる。
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include "core/frame.hpp"
#include "core/timing.hpp"
#include "core/types.hpp"
#include "infer/ort_engine.hpp"

namespace omask::plugin {

// 解析スレッドからの知らせ（解析スレッドで呼ばれる。止めた後は呼ばれない）
class AnalysisListener {
public:
	virtual ~AnalysisListener() = default;
	virtual void on_engine_report(const infer::EngineReport &report, double prepare_ms) = 0;
	virtual void on_gpu_fallback() = 0;
	virtual void on_engine_failed() = 0;
	virtual void on_result(uint64_t generation, const core::AnalysisResult &result) = 0;
};

// 解析の所要時間の集計（全体と、前処理・推論の内訳）
struct AnalysisTiming {
	core::TimingSummary total;
	core::TimingSummary preprocess;
	core::TimingSummary inference;
};

class AnalysisWorker {
public:
	static constexpr int kPoolSize = 4; // 同時に解析を待てるコマの数（PRC-01）

	AnalysisWorker(infer::EngineOptions options, AnalysisListener *listener);
	~AnalysisWorker();
	AnalysisWorker(const AnalysisWorker &) = delete;
	AnalysisWorker &operator=(const AnalysisWorker &) = delete;

	// 解析スレッドを起こし、推論エンジンの準備を始める。起こせなければ偽
	bool start();

	// 推論エンジンの準備が済み、頭上タグを推論できる
	bool ready() const;
	bool tag_available() const;
	bool screen_available() const;

	// 描画スレッドから: コマの画素を空いた解析用のバッファに写して待ち行列に入れる。空きが無ければ偽（解析なし）。
	// frame は並んでいるままの見え方、flip は上下が逆に並んでいるか
	bool submit(const core::FrameView &frame, bool flip, int64_t timestamp_ns, uint64_t generation);

	// 解析の所要時間の集計を取り出す
	AnalysisTiming take_timing();

	// 止める。timeout までに終わらなければ解析スレッドを切り離して偽を返す（ERR-10）。以後、知らせは来ない
	bool stop(std::chrono::milliseconds timeout);

private:
	struct State;
	static void run(std::shared_ptr<State> state);

	std::shared_ptr<State> state_;
	std::thread thread_;
};

} // namespace omask::plugin
