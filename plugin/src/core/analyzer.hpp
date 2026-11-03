// 1 コマの解析（処理詳細設計書 PRC-04 ＋ PRC-02）。前処理 → 画面判定 → （バトル中・不確かなら）頭上タグの推論 →
// 候補の取り出し。プラグインの解析スレッドと検証用 CLI（omask-eval）で同じものを使う。
#pragma once

#include <cstdint>
#include <vector>

#include "core/frame.hpp"
#include "core/inference.hpp"
#include "core/params.hpp"
#include "core/screen_judge.hpp"
#include "core/types.hpp"

namespace omask::core {

// 直前の解析の所要時間の内訳（PRC-09 の集計に使う）
struct AnalyzerTiming {
	double preprocess_ms = 0.0;
	double screen_ms = 0.0;
	double tags_ms = 0.0; // 頭上タグを推論しなかったコマは 0
};

class Analyzer {
public:
	explicit Analyzer(const FixedParams &fixed_params = FixedParams{}, const TagParams &tag_params = TagParams{});

	// backend が nullptr なら推論しない（画面は不確か・確率なし、頭上タグは無し）
	AnalysisResult analyze(const FrameView &frame, int64_t timestamp_ns, InferenceBackend *backend);

	// 時刻の飛びでやり直すとき
	void reset() { judge_.reset(); }

	const AnalyzerTiming &last_timing() const { return timing_; }

private:
	AnalyzerTiming timing_;
	ScreenJudge judge_;
	float candidate_threshold_;
	std::vector<float> tensor_;
	std::vector<float> tag_output_;
};

} // namespace omask::core
