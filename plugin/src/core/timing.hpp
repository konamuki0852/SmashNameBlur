// 所要時間の集計（処理詳細設計書 PRC-09 手順 3: 60 秒ごとに平均と 99%）。スレッドに安全ではない（使う側がロックする）。
#pragma once

#include <cstddef>
#include <vector>

namespace omask::core {

struct TimingSummary {
	std::size_t count = 0;
	double average_ms = 0.0;
	double p99_ms = 0.0; // 99 パーセンタイル（小さいほうから 99% の位置の値）
	double max_ms = 0.0;
};

class TimingWindow {
public:
	static constexpr std::size_t kMaxSamples = 8192; // 60fps で 2 分強。超えた分は数えない（メモリを増やさない）

	void add(double ms);
	// 集計して空にする
	TimingSummary take();

private:
	std::vector<double> samples_;
};

} // namespace omask::core
