// 状態の監視（処理詳細設計書 PRC-09、画面設計書 図 5-2）とログの間引き。時刻は呼ぶ側が渡す（テストできるように）。
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>

namespace omask::core {

// フィルターの状態（ENT-08 state）
enum class EngineState { kPreparing, kRunningGpu, kRunningCpu, kLagging, kUnavailable, kUnsupportedFormat };

// 「検出できない」の理由（ERR-04 の {理由}。言語ファイルの文言に直す）
enum class UnavailableReason { kNone, kRuntimeMissing, kRuntimeVersion, kModelMissing, kModelFormat, kInferenceFailed };

class StatusMonitor {
public:
	static constexpr int64_t kWindowNs = 60LL * 1000000000; // 「間に合わない」を数える時間（60 秒）
	static constexpr int kLagThreshold = 5;                 // この回数以上で「追いついていない」

	void on_engine_ready(bool gpu);
	void on_engine_failed(UnavailableReason reason);
	void on_gpu_fallback();
	void on_late(int64_t now_ns);
	void on_tick(int64_t now_ns); // 古い「間に合わない」を数えから外す
	void on_format(bool supported);

	EngineState state() const;
	UnavailableReason reason() const { return reason_; }
	int late_count() const { return static_cast<int>(late_ns_.size()); }

private:
	EngineState base_ = EngineState::kPreparing; // 準備中・動作中（GPU／CPU）・検出できない
	UnavailableReason reason_ = UnavailableReason::kNone;
	std::deque<int64_t> late_ns_;
	bool lagging_ = false;
	bool unsupported_ = false;
};

// 同じ内容のログを、決まった間隔（既定 60 秒）に 1 回までにする
class LogLimiter {
public:
	explicit LogLimiter(int64_t interval_ns = 60LL * 1000000000) : interval_ns_(interval_ns) {}
	bool allow(const std::string &key, int64_t now_ns);

private:
	int64_t interval_ns_;
	std::map<std::string, int64_t> last_ns_;
};

} // namespace omask::core
