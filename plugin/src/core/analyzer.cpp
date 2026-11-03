#include "core/analyzer.hpp"

#include <array>
#include <chrono>

#include "core/preprocess.hpp"
#include "core/result_band.hpp"
#include "core/tag_decode.hpp"

namespace omask::core {

namespace {

using Clock = std::chrono::steady_clock;

double ms_between(Clock::time_point from, Clock::time_point to)
{
	return std::chrono::duration<double, std::milli>(to - from).count();
}

} // namespace

Analyzer::Analyzer(const FixedParams &fixed_params, const TagParams &tag_params)
	: judge_(fixed_params),
	  candidate_threshold_(tag_params.link_threshold),
	  tensor_(kTensorSize),
	  tag_output_(static_cast<size_t>(3) * kTagGridWidth * kTagGridHeight)
{
}

AnalysisResult Analyzer::analyze(const FrameView &frame, int64_t timestamp_ns, InferenceBackend *backend)
{
	AnalysisResult result;
	result.timestamp_ns = timestamp_ns;
	const Clock::time_point start = Clock::now();
	result.brightness = preprocess_frame(frame, tensor_.data());
	const Clock::time_point preprocessed = Clock::now();

	// PRC-02: 画面判定（使えなければ不確か・確率なし）
	const bool screen_usable = backend != nullptr && backend->screen_available();
	std::array<float, 4> logits{};
	bool screen_failed = false;
	if (screen_usable && !backend->run_screen(tensor_.data(), logits))
		screen_failed = true;
	const Clock::time_point judged = Clock::now();
	const ScreenDecision decision = judge_.decide(screen_usable && !screen_failed ? logits.data() : nullptr,
						      result.brightness, timestamp_ns);
	result.screen_kind = decision.kind;
	result.has_screen_probs = decision.has_probs;
	result.screen_probs = decision.probs;
	result.screen_by_brightness = decision.by_brightness;
	// PRC-14: 黒い帯はどの画面でも探す（カードが入ってくる途中は、まだリザルトと判定されていないことがある）
	result.result_bands = find_result_bands(tensor_.data());

	// PRC-04 手順 3〜5: バトル中・不確か、または screennet を使えないときだけ頭上タグを推論する
	const bool need_tags = decision.kind == ScreenKind::kBattle || decision.kind == ScreenKind::kUncertain;
	bool tag_failed = false;
	if (backend != nullptr && backend->tag_available() && need_tags) {
		if (backend->run_tags(tensor_.data(), tag_output_))
			result.tags = decode_tag_candidates(tag_output_.data(), candidate_threshold_);
		else
			tag_failed = true;
	}
	result.status = screen_failed || tag_failed ? AnalysisStatus::kFailed : AnalysisStatus::kOk;
	timing_.preprocess_ms = ms_between(start, preprocessed);
	timing_.screen_ms = ms_between(preprocessed, judged);
	timing_.tags_ms = ms_between(judged, Clock::now());
	return result;
}

} // namespace omask::core
