// 遅延バッファの論理（処理詳細設計書 PRC-01 手順 2・4・6・7・後始末）。OBS の「映像の遅延（非同期）」と同じ判定。
// コマの持ち方は型引数で受ける（プラグインでは obs_source_frame*、テストでは偽のコマ）。
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <utility>

namespace omask::core {

template<class Frame> class DelayBuffer {
public:
	static constexpr int64_t kJumpNs = 1000000000; // 時刻の飛びの基準（1 秒）

	struct PushResult {
		bool time_jump = false;      // 溜めたコマを返してやり直した（結果の置き場と決まりの状態も空にする）
		std::optional<Frame> output; // 出力するコマ u（遅延が満ちるまでは無い）
	};

	// コマ t を入れる。release は、出力せずに OBS へ返すコマに対して呼ぶ
	template<class Release> PushResult push(Frame frame, int64_t timestamp_ns, int64_t delay_ns, Release &&release)
	{
		PushResult result;
		if (!has_last_ || timestamp_ns < last_ns_ || timestamp_ns - last_ns_ > kJumpNs) {
			result.time_jump = has_last_;
			release_all(release);
		}
		has_last_ = true;
		last_ns_ = timestamp_ns;
		frames_.push_back(Item{std::move(frame), timestamp_ns});
		if (!reached_ && timestamp_ns - frames_.front().timestamp_ns < delay_ns)
			return result;
		reached_ = true;
		result.output = std::move(frames_.front().frame);
		frames_.pop_front();
		return result;
	}

	// 溜めたコマをすべて返す（外す・破棄・時刻の飛び）
	template<class Release> void release_all(Release &&release)
	{
		while (!frames_.empty()) {
			release(std::move(frames_.front().frame));
			frames_.pop_front();
		}
		reached_ = false;
	}

	std::size_t size() const { return frames_.size(); }
	bool reached() const { return reached_; }

	// 溜めているコマの時刻（解析の結果が「受け取り済み」かを判断するのに使う）
	template<class Fn> void for_each_timestamp(Fn &&fn) const
	{
		for (const Item &item : frames_)
			fn(item.timestamp_ns);
	}

private:
	struct Item {
		Frame frame;
		int64_t timestamp_ns;
	};
	std::deque<Item> frames_;
	bool reached_ = false;
	bool has_last_ = false;
	int64_t last_ns_ = 0;
};

} // namespace omask::core
