// SCR-01 の状態（I01）と映像の遅れ（I03）の文（画面設計書 §7.1、非機能・運用設計書 ERR-01〜05・07）。
// 文言は言語ファイル（data/locale）から引く。ログに出す状態・理由の名前（英語の定型）もここに置く。
#pragma once

#include <obs-properties.h>

#include <cstdint>
#include <string>

#include "core/status.hpp"

namespace omask::plugin {

struct StatusText {
	std::string text;
	enum obs_text_info_type type = OBS_TEXT_INFO_NORMAL;
};

StatusText status_text(core::EngineState state, core::UnavailableReason reason, int late_count,
		       const std::string &format);

// 「約 133 ms（60 fps）」
std::string delay_text(int64_t delay_ns, int64_t interval_ns);

// ログの定型（英語）
const char *state_name(core::EngineState state);
const char *reason_name(core::UnavailableReason reason);

} // namespace omask::plugin
