// 画面判定の扱い（処理詳細設計書 PRC-02）。screennet のロジットから種類を決め、白いコマの決まりを当てる。
#pragma once

#include <array>
#include <cstdint>

#include "core/params.hpp"
#include "core/types.hpp"

namespace omask::core {

struct ScreenDecision {
	ScreenKind kind = ScreenKind::kUncertain;
	bool has_probs = false;
	std::array<float, 4> probs{}; // other・vs・battle・result
	bool by_brightness = false;   // 白いコマの決まりで vs にした
};

class ScreenJudge {
public:
	explicit ScreenJudge(const FixedParams &params = FixedParams{}) : params_(params) {}

	// logits は 4 つ（other・vs・battle・result）。screennet を使えないコマは nullptr（不確か・確率なし）。
	// brightness は入力テンソルの全要素の平均、timestamp_ns はコマの時刻（時刻の順に呼ぶ）
	ScreenDecision decide(const float *logits, float brightness, int64_t timestamp_ns);

	// 時刻の飛びでやり直すとき
	void reset() { has_recent_battle_ = false; }

private:
	FixedParams params_;
	bool has_recent_battle_ = false;
	int64_t recent_battle_ns_ = 0; // 最後にバトル・リザルトの判定があったコマの時刻
};

} // namespace omask::core
