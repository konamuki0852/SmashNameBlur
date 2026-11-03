// 遅延 D の決め方（処理詳細設計書 PRC-01）。D = 先読み ＋ 解析の余裕（コマ数）× コマの間隔。
// コマの間隔は直近 30 コマの時刻の差の中央値（最初は 1/60 秒）。
#pragma once

#include <cstdint>
#include <deque>

namespace omask::core {

class DelayEstimator {
public:
	static constexpr int kWindow = 30;                      // 間隔を見るコマの数
	static constexpr int64_t kDefaultIntervalNs = 16666667; // 最初は 60fps とみなす
	static constexpr int64_t kMaxIntervalNs = 1000000000;   // これを超える間隔は時刻の飛びとして数えない

	void add_timestamp(int64_t timestamp_ns);
	int64_t frame_interval_ns() const;
	int64_t delay_ns(int look_ahead_ms, int margin_frames) const;
	void reset();

private:
	std::deque<int64_t> intervals_;
	bool has_last_ = false;
	int64_t last_ns_ = 0;
};

} // namespace omask::core
