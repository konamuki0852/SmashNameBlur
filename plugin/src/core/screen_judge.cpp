#include "core/screen_judge.hpp"

#include <algorithm>
#include <cmath>

namespace omask::core {

ScreenDecision ScreenJudge::decide(const float *logits, float brightness, int64_t timestamp_ns)
{
	ScreenDecision decision;
	if (logits == nullptr)
		return decision; // screennet を使えない: 不確か・確率なし（PRC-03 で全部有効になる）

	// ソフトマックス
	const float top_logit = *std::max_element(logits, logits + 4);
	float total = 0.0f;
	for (int i = 0; i < 4; ++i) {
		decision.probs[static_cast<size_t>(i)] = std::exp(logits[i] - top_logit);
		total += decision.probs[static_cast<size_t>(i)];
	}
	for (float &p : decision.probs)
		p /= total;
	decision.has_probs = true;
	const auto top = static_cast<int>(std::max_element(decision.probs.begin(), decision.probs.end()) -
					  decision.probs.begin());
	decision.kind = decision.probs[static_cast<size_t>(top)] >= params_.decision_threshold - kProbabilityEpsilon
				? static_cast<ScreenKind>(top)
				: ScreenKind::kUncertain;

	// バトル・リザルトの判定（不確かでその確率が下限以上を含む）の時刻を覚える
	const bool battle_like = decision.kind == ScreenKind::kBattle || decision.kind == ScreenKind::kResult ||
				 (decision.kind == ScreenKind::kUncertain &&
				  (decision.probs[2] >= params_.uncertain_floor - kProbabilityEpsilon ||
				   decision.probs[3] >= params_.uncertain_floor - kProbabilityEpsilon));
	if (battle_like) {
		has_recent_battle_ = true;
		recent_battle_ns_ = timestamp_ns;
	}

	// 白いコマの決まり: ほぼ真っ白で、直前の止める時間（ちょうどを含む）にバトル・リザルトの判定が無ければ vs
	const int64_t gate_ns = static_cast<int64_t>(params_.bright_gate_ms) * kNsPerMs;
	const bool gated = has_recent_battle_ && timestamp_ns - recent_battle_ns_ <= gate_ns;
	if (brightness > params_.bright_threshold && !gated) {
		decision.kind = ScreenKind::kVs;
		decision.by_brightness = true;
	}
	return decision;
}

} // namespace omask::core
